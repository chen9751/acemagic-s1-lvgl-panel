'use strict';

const node_hid = require('node-hid');
const lcd = require('./legacy/lcd_device');

node_hid.setDriverType('libusb');

const WIDTH = 170;
const HEIGHT = 320;
const PIXELS = WIDTH * HEIGHT;
const FRAME_BYTES = PIXELS * 2;

/*
 * The verified S1 scan order is 320 x 170 after rotating the LVGL
 * 170 x 320 portrait framebuffer. Keep this mapping unchanged.
 */
const HW_WIDTH = HEIGHT;
const HW_HEIGHT = WIDTH;

/*
 * LCD_REFRESH has one 4096-byte pixel payload, i.e. at most 2048
 * RGB565 pixels per HID write. 64 x 32 exactly fills that payload.
 */
const TILE_WIDTH = 64;
const TILE_HEIGHT = 32;
const MAX_PARTIAL_TILES = 18;

const ERROR_BACKOFF_MS = 250;
const HEARTBEAT_INTERVAL_MS = 5000;
const STATS_INTERVAL_MS = 5000;

let handle = null;
let rx = Buffer.alloc(0);
let drawing = false;
let pendingFrame = null;
let drawTimer = null;
let retryNotBefore = 0;

/*
 * Shadow of what has actually reached the LCD successfully, in hardware
 * 320 x 170 scan order. Partial writes update only their successful tile.
 */
let displayedPixels = null;

let statsReceived = 0;
let statsDuplicate = 0;
let statsReplaced = 0;
let statsFull = 0;
let statsPartialFrames = 0;
let statsPartialTiles = 0;
let statsErrors = 0;
let statsLastTransferMs = 0;
let statsMaxTransferMs = 0;

function frameToHardwarePixels(frame) {
    const src = new Uint16Array(PIXELS);
    const pixels = new Uint16Array(PIXELS);

    for (let i = 0; i < PIXELS; i++) {
        src[i] = frame.readUInt16LE(i * 2);
    }

    /*
     * LVGL source:
     *   170 x 320 portrait
     *
     * S1 hardware scan order:
     *   320 x 170
     *
     * This mapping is verified on real hardware. Do not alter it.
     */
    for (let y = 0; y < HEIGHT; y++) {
        for (let x = 0; x < WIDTH; x++) {
            const srcIndex = y * WIDTH + x;
            const dstX = y;
            const dstY = WIDTH - 1 - x;
            const dstIndex = dstY * HW_WIDTH + dstX;
            pixels[dstIndex] = src[srcIndex];
        }
    }

    return pixels;
}

function hardwarePixelsEqual(a, b) {
    if (!a || !b || a.length !== b.length) {
        return false;
    }

    for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i]) {
            return false;
        }
    }

    return true;
}

function tileChanged(current, displayed, tile) {
    for (let y = 0; y < tile.height; y++) {
        const rowStart = (tile.y + y) * HW_WIDTH + tile.x;

        for (let x = 0; x < tile.width; x++) {
            const index = rowStart + x;
            if (current[index] !== displayed[index]) {
                return true;
            }
        }
    }

    return false;
}

function findChangedTiles(current, displayed) {
    const changed = [];

    for (let y = 0; y < HW_HEIGHT; y += TILE_HEIGHT) {
        const height = Math.min(TILE_HEIGHT, HW_HEIGHT - y);

        for (let x = 0; x < HW_WIDTH; x += TILE_WIDTH) {
            const width = Math.min(TILE_WIDTH, HW_WIDTH - x);
            const tile = { x, y, width, height };

            if (tileChanged(current, displayed, tile)) {
                changed.push(tile);
            }
        }
    }

    return changed;
}

function tileImage(pixels, tile) {
    const data = new Uint16Array(tile.width * tile.height);
    let dst = 0;

    for (let y = 0; y < tile.height; y++) {
        const srcStart = (tile.y + y) * HW_WIDTH + tile.x;

        for (let x = 0; x < tile.width; x++) {
            data[dst++] = pixels[srcStart + x];
        }
    }

    return { data };
}

function copyTileToDisplayed(current, tile) {
    for (let y = 0; y < tile.height; y++) {
        const start = (tile.y + y) * HW_WIDTH + tile.x;
        displayedPixels.set(
            current.subarray(start, start + tile.width),
            start
        );
    }
}

function scheduleDraw() {
    if (drawing || drawTimer || !pendingFrame || !handle) {
        return;
    }

    const delay = Math.max(0, retryNotBefore - Date.now());

    drawTimer = setTimeout(() => {
        drawTimer = null;
        void pumpDraw();
    }, delay);
}

function queueFrame(frame) {
    statsReceived++;

    /*
     * Avoid decoding/reordering the very common identical LVGL frames.
     * The currently displayed hardware shadow remains the authority for
     * deciding whether an update was really delivered.
     */
    if (pendingFrame && frame.equals(pendingFrame)) {
        statsDuplicate++;
        return;
    }

    if (pendingFrame) {
        statsReplaced++;
    }

    pendingFrame = Buffer.from(frame);
    scheduleDraw();
}

async function sendFull(currentPixels) {
    await lcd.redraw(handle, { data: currentPixels });
    displayedPixels = new Uint16Array(currentPixels);
    statsFull++;
}

async function sendPartial(currentPixels, changedTiles) {
    for (const tile of changedTiles) {
        await lcd.refresh(
            handle,
            tile.x,
            tile.y,
            tile.width,
            tile.height,
            tileImage(currentPixels, tile)
        );

        /*
         * Commit the shadow tile only after that exact HID write succeeds.
         * If a later tile fails, the next frame automatically retries only
         * the parts which are still different from the real LCD state.
         */
        copyTileToDisplayed(currentPixels, tile);
        statsPartialTiles++;
    }

    statsPartialFrames++;
}

async function pumpDraw() {
    if (drawing || !pendingFrame || !handle) {
        return;
    }

    const frame = pendingFrame;
    pendingFrame = null;
    drawing = true;

    const startedAt = Date.now();
    let mode = 'none';
    let changedTiles = [];

    try {
        const currentPixels = frameToHardwarePixels(frame);

        if (!displayedPixels) {
            mode = 'full';
            await sendFull(currentPixels);
        } else if (hardwarePixelsEqual(currentPixels, displayedPixels)) {
            statsDuplicate++;
        } else {
            changedTiles = findChangedTiles(currentPixels, displayedPixels);

            if (changedTiles.length === 0) {
                statsDuplicate++;
            } else if (changedTiles.length <= MAX_PARTIAL_TILES) {
                mode = 'partial';
                await sendPartial(currentPixels, changedTiles);
            } else {
                mode = 'full';
                await sendFull(currentPixels);
            }
        }

        retryNotBefore = 0;
    } catch (err) {
        statsErrors++;

        /*
         * A failed 27-packet full redraw can leave the physical panel in an
         * unknown intermediate state. Forget the shadow so the next frame
         * performs a complete resynchronization. Partial writes are different:
         * each successful tile was committed individually and can be trusted.
         */
        if (mode === 'full') {
            displayedPixels = null;
        }

        console.error(
            `LCD ${mode} refresh error${changedTiles.length ? ` (${changedTiles.length} tiles)` : ''}:`,
            err
        );
        retryNotBefore = Date.now() + ERROR_BACKOFF_MS;
    } finally {
        statsLastTransferMs = Date.now() - startedAt;
        if (statsLastTransferMs > statsMaxTransferMs) {
            statsMaxTransferMs = statsLastTransferMs;
        }

        drawing = false;
        scheduleDraw();
    }
}

async function main() {
    const device = node_hid.devices().find(d =>
        d.vendorId === 0x04d9 &&
        d.productId === 0xfd01 &&
        d.interface === 1
    );

    if (!device) {
        throw new Error('S1 LCD 04d9:fd01 interface 1 not found');
    }

    console.error(`S1 LCD found: ${device.path}, interface ${device.interface}`);

    handle = await node_hid.HIDAsync.open(device.path);

    console.error(
        `S1 LCD opened; partial refresh ${TILE_WIDTH}x${TILE_HEIGHT}, ` +
        `full fallback above ${MAX_PARTIAL_TILES} changed tiles`
    );

    await lcd.set_orientation(handle, true);

    process.stdin.on('data', chunk => {
        rx = Buffer.concat([rx, chunk]);

        while (rx.length >= FRAME_BYTES) {
            const frame = Buffer.from(rx.subarray(0, FRAME_BYTES));
            rx = rx.subarray(FRAME_BYTES);
            queueFrame(frame);
        }
    });

    process.stdin.resume();

    setInterval(async () => {
        if (!drawing && !drawTimer && !pendingFrame && handle) {
            try {
                await lcd.heartbeat(handle);
            } catch (_) {
                /* Heartbeat failures are non-fatal and intentionally quiet. */
            }
        }
    }, HEARTBEAT_INTERVAL_MS);

    setInterval(() => {
        console.error(
            `LCD stats: rx=${statsReceived} duplicate=${statsDuplicate} replaced=${statsReplaced} ` +
            `full=${statsFull} partial=${statsPartialFrames} tiles=${statsPartialTiles} ` +
            `errors=${statsErrors} transfer=${statsLastTransferMs}ms max=${statsMaxTransferMs}ms`
        );

        statsReceived = 0;
        statsDuplicate = 0;
        statsReplaced = 0;
        statsFull = 0;
        statsPartialFrames = 0;
        statsPartialTiles = 0;
        statsErrors = 0;
        statsMaxTransferMs = statsLastTransferMs;
    }, STATS_INTERVAL_MS);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
