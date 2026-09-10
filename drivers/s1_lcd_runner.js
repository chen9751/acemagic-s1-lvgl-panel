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
 * LCD_REFRESH carries at most 4096 bytes = 2048 RGB565 pixels.
 * A 12 x 170 strip is 2040 pixels, so one full-height hardware strip fits
 * in a single HID write. In LVGL portrait space that corresponds to a
 * 12-pixel-high horizontal band, which matches this UI much better than
 * the old 64 x 32 grid.
 */
const STRIP_WIDTH = 12;
const MAX_REFRESH_RETRIES = 2;
const RETRY_DELAY_MS = 30;
const ERROR_BACKOFF_MS = 150;
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
 * 320 x 170 scan order. Each successful strip is committed individually.
 */
let displayedPixels = null;

let statsReceived = 0;
let statsDuplicate = 0;
let statsReplaced = 0;
let statsInitialFull = 0;
let statsPartialFrames = 0;
let statsPartialStrips = 0;
let statsRetries = 0;
let statsErrors = 0;
let statsLastTransferMs = 0;
let statsMaxTransferMs = 0;

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

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

function stripChanged(current, displayed, strip) {
    for (let y = 0; y < strip.height; y++) {
        const rowStart = (strip.y + y) * HW_WIDTH + strip.x;

        for (let x = 0; x < strip.width; x++) {
            const index = rowStart + x;
            if (current[index] !== displayed[index]) {
                return true;
            }
        }
    }

    return false;
}

function findChangedStrips(current, displayed) {
    const changed = [];

    for (let x = 0; x < HW_WIDTH; x += STRIP_WIDTH) {
        const width = Math.min(STRIP_WIDTH, HW_WIDTH - x);
        const strip = {
            x,
            y: 0,
            width,
            height: HW_HEIGHT
        };

        if (stripChanged(current, displayed, strip)) {
            changed.push(strip);
        }
    }

    return changed;
}

function stripImage(pixels, strip) {
    const data = new Uint16Array(strip.width * strip.height);
    let dst = 0;

    for (let y = 0; y < strip.height; y++) {
        const srcStart = (strip.y + y) * HW_WIDTH + strip.x;

        for (let x = 0; x < strip.width; x++) {
            data[dst++] = pixels[srcStart + x];
        }
    }

    return { data };
}

function copyStripToDisplayed(current, strip) {
    for (let y = 0; y < strip.height; y++) {
        const start = (strip.y + y) * HW_WIDTH + strip.x;
        displayedPixels.set(
            current.subarray(start, start + strip.width),
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

    if (pendingFrame && frame.equals(pendingFrame)) {
        statsDuplicate++;
        return;
    }

    if (pendingFrame) {
        statsReplaced++;
    }

    /* Latest-frame-wins while the LCD is busy. */
    pendingFrame = Buffer.from(frame);
    scheduleDraw();
}

async function sendInitialFull(currentPixels) {
    await lcd.redraw(handle, { data: currentPixels });
    displayedPixels = new Uint16Array(currentPixels);
    statsInitialFull++;
}

async function refreshStripWithRetry(currentPixels, strip) {
    const image = stripImage(currentPixels, strip);
    let lastError = null;

    for (let attempt = 0; attempt <= MAX_REFRESH_RETRIES; attempt++) {
        try {
            await lcd.refresh(
                handle,
                strip.x,
                strip.y,
                strip.width,
                strip.height,
                image
            );

            copyStripToDisplayed(currentPixels, strip);
            statsPartialStrips++;
            return;
        } catch (err) {
            lastError = err;

            if (attempt < MAX_REFRESH_RETRIES) {
                statsRetries++;
                await sleep(RETRY_DELAY_MS);
            }
        }
    }

    throw lastError;
}

async function sendPartial(currentPixels, changedStrips) {
    for (const strip of changedStrips) {
        await refreshStripWithRetry(currentPixels, strip);
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
    let changedStrips = [];

    try {
        const currentPixels = frameToHardwarePixels(frame);

        /*
         * Only the very first synchronization uses the slow 27-packet full
         * redraw. After a shadow exists, every change uses LCD_REFRESH strips,
         * including whole-page transitions. This avoids repeatedly falling
         * back to the 1-3 second redraw path.
         */
        if (!displayedPixels) {
            mode = 'initial-full';
            await sendInitialFull(currentPixels);
        } else if (hardwarePixelsEqual(currentPixels, displayedPixels)) {
            statsDuplicate++;
        } else {
            changedStrips = findChangedStrips(currentPixels, displayedPixels);

            if (changedStrips.length === 0) {
                statsDuplicate++;
            } else {
                mode = 'partial';
                await sendPartial(currentPixels, changedStrips);
            }
        }

        retryNotBefore = 0;
    } catch (err) {
        statsErrors++;

        /*
         * Never abandon the target frame after a failed partial transfer.
         * Successfully written strips are already reflected in displayedPixels;
         * re-queueing the same frame means the next pass automatically sends
         * only the strips that are still missing. A newer LVGL frame wins if
         * one is already waiting.
         */
        if (!pendingFrame) {
            pendingFrame = frame;
        }

        if (mode === 'initial-full') {
            displayedPixels = null;
        }

        console.error(
            `LCD ${mode} refresh error${changedStrips.length ? ` (${changedStrips.length} strips)` : ''}:`,
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
        `S1 LCD opened; ${STRIP_WIDTH}x${HW_HEIGHT} strip partial refresh, ` +
        `full redraw only for initial sync`
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
            `initialFull=${statsInitialFull} partial=${statsPartialFrames} strips=${statsPartialStrips} ` +
            `retries=${statsRetries} errors=${statsErrors} ` +
            `transfer=${statsLastTransferMs}ms max=${statsMaxTransferMs}ms`
        );

        statsReceived = 0;
        statsDuplicate = 0;
        statsReplaced = 0;
        statsInitialFull = 0;
        statsPartialFrames = 0;
        statsPartialStrips = 0;
        statsRetries = 0;
        statsErrors = 0;
        statsMaxTransferMs = statsLastTransferMs;
    }, STATS_INTERVAL_MS);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
