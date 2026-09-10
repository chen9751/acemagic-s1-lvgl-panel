'use strict';

const node_hid = require('node-hid');
const lcd = require('./legacy/lcd_device');

node_hid.setDriverType('libusb');

const WIDTH = 170;
const HEIGHT = 320;
const PIXELS = WIDTH * HEIGHT;

const HW_WIDTH = HEIGHT;
const HW_HEIGHT = WIDTH;

const PIPE_HEADER_SIZE = 18;
const PIPE_MAGIC = Buffer.from('S1UP');
const PIPE_VERSION = 1;
const PIPE_PARTIAL = 1;
const PIPE_FULL = 2;

const MAX_REFRESH_PIXELS = 2048;
const MAX_REFRESH_WIDTH = 255;
const MAX_REFRESH_HEIGHT = 255;

/*
 * If a dirty rectangle covers most of the logical screen, prefer the
 * firmware's redraw command. It is slower, but it avoids visibly painting
 * a new page in many separate LCD_REFRESH operations.
 */
const FULL_REDRAW_AREA_RATIO = 0.60;
const ERROR_BACKOFF_MS = 150;
const HEARTBEAT_INTERVAL_MS = 5000;
const STATS_INTERVAL_MS = 5000;

let handle = null;
let rx = Buffer.alloc(0);
let drawing = false;
let drawTimer = null;
let retryNotBefore = 0;

/* Latest logical 170x320 RGB565 image reconstructed from HAL messages. */
const logicalPixels = new Uint16Array(PIXELS);
let logicalHasData = false;

/*
 * Pending dirty state on the Node side. HAL may keep writing while a HID
 * transfer is in progress, so merge new areas into the latest logical image
 * instead of queueing stale rectangles one by one.
 */
let pendingRect = null;
let pendingFull = false;

let statsMessages = 0;
let statsMerged = 0;
let statsFull = 0;
let statsPartial = 0;
let statsChunks = 0;
let statsErrors = 0;
let statsLastTransferMs = 0;
let statsMaxTransferMs = 0;

function readU16LE(buffer, offset) {
    return buffer[offset] | (buffer[offset + 1] << 8);
}

function readU32LE(buffer, offset) {
    return (
        buffer[offset] |
        (buffer[offset + 1] << 8) |
        (buffer[offset + 2] << 16) |
        (buffer[offset + 3] << 24)
    ) >>> 0;
}

function mergeRect(a, b) {
    if (!a) {
        return { ...b };
    }

    const x1 = Math.min(a.x, b.x);
    const y1 = Math.min(a.y, b.y);
    const x2 = Math.max(a.x + a.width - 1, b.x + b.width - 1);
    const y2 = Math.max(a.y + a.height - 1, b.y + b.height - 1);

    return {
        x: x1,
        y: y1,
        width: x2 - x1 + 1,
        height: y2 - y1 + 1
    };
}

function isFullLogicalRect(rect) {
    return rect.x === 0 &&
        rect.y === 0 &&
        rect.width === WIDTH &&
        rect.height === HEIGHT;
}

function shouldUseFullRedraw(rect, requestedFull) {
    if (requestedFull || isFullLogicalRect(rect)) {
        return true;
    }

    const area = rect.width * rect.height;
    return area >= Math.floor(PIXELS * FULL_REDRAW_AREA_RATIO);
}

function applyLogicalRegion(rect, payload) {
    const expectedBytes = rect.width * rect.height * 2;

    if (payload.length !== expectedBytes) {
        throw new Error(
            `invalid S1 update payload ${payload.length}/${expectedBytes}`
        );
    }

    let srcOffset = 0;

    for (let row = 0; row < rect.height; row++) {
        const dstRow = (rect.y + row) * WIDTH + rect.x;

        for (let col = 0; col < rect.width; col++) {
            logicalPixels[dstRow + col] = payload.readUInt16LE(srcOffset);
            srcOffset += 2;
        }
    }

    logicalHasData = true;
}

function parsePipeMessages() {
    for (;;) {
        if (rx.length < PIPE_HEADER_SIZE) {
            return;
        }

        if (!rx.subarray(0, 4).equals(PIPE_MAGIC)) {
            const nextMagic = rx.indexOf(PIPE_MAGIC, 1);

            if (nextMagic < 0) {
                rx = rx.subarray(Math.max(0, rx.length - 3));
                return;
            }

            console.error(`S1 LCD: resyncing pipe stream, skipped ${nextMagic} bytes`);
            rx = rx.subarray(nextMagic);

            if (rx.length < PIPE_HEADER_SIZE) {
                return;
            }
        }

        const version = rx[4];
        const type = rx[5];
        const rect = {
            x: readU16LE(rx, 6),
            y: readU16LE(rx, 8),
            width: readU16LE(rx, 10),
            height: readU16LE(rx, 12)
        };
        const payloadBytes = readU32LE(rx, 14);

        if (version !== PIPE_VERSION) {
            throw new Error(`unsupported S1 pipe protocol version ${version}`);
        }

        if (type !== PIPE_PARTIAL && type !== PIPE_FULL) {
            throw new Error(`unsupported S1 pipe update type ${type}`);
        }

        if (rect.width === 0 || rect.height === 0 ||
            rect.x + rect.width > WIDTH ||
            rect.y + rect.height > HEIGHT) {
            throw new Error(
                `invalid S1 update rect ${rect.x},${rect.y} ${rect.width}x${rect.height}`
            );
        }

        const expectedBytes = rect.width * rect.height * 2;

        if (payloadBytes !== expectedBytes || payloadBytes > WIDTH * HEIGHT * 2) {
            throw new Error(
                `invalid S1 update size ${payloadBytes}, expected ${expectedBytes}`
            );
        }

        const totalBytes = PIPE_HEADER_SIZE + payloadBytes;

        if (rx.length < totalBytes) {
            return;
        }

        const payload = rx.subarray(PIPE_HEADER_SIZE, totalBytes);
        applyLogicalRegion(rect, payload);
        rx = rx.subarray(totalBytes);

        statsMessages++;

        if (pendingRect) {
            statsMerged++;
        }

        pendingRect = mergeRect(pendingRect, rect);
        pendingFull = pendingFull || type === PIPE_FULL;
        scheduleDraw();
    }
}

function fullHardwareImage() {
    const pixels = new Uint16Array(PIXELS);

    /*
     * Verified mapping. Do not change:
     * LVGL (x,y) -> S1 hardware (y, 169-x)
     */
    for (let y = 0; y < HEIGHT; y++) {
        for (let x = 0; x < WIDTH; x++) {
            const srcIndex = y * WIDTH + x;
            const dstX = y;
            const dstY = WIDTH - 1 - x;
            const dstIndex = dstY * HW_WIDTH + dstX;
            pixels[dstIndex] = logicalPixels[srcIndex];
        }
    }

    return { data: pixels };
}

function logicalRectToHardware(rect) {
    const hwRect = {
        x: rect.y,
        y: WIDTH - (rect.x + rect.width),
        width: rect.height,
        height: rect.width
    };

    const data = new Uint16Array(hwRect.width * hwRect.height);

    /*
     * Within the rotated rectangle:
     *   hardware dx = logical local y
     *   hardware dy = logical width - 1 - logical local x
     */
    for (let dy = 0; dy < hwRect.height; dy++) {
        const localX = rect.width - 1 - dy;

        for (let dx = 0; dx < hwRect.width; dx++) {
            const localY = dx;
            const srcIndex =
                (rect.y + localY) * WIDTH + (rect.x + localX);
            data[dy * hwRect.width + dx] = logicalPixels[srcIndex];
        }
    }

    return { rect: hwRect, data };
}

function chooseChunkShape(width, height) {
    let best = null;

    const maxW = Math.min(width, MAX_REFRESH_WIDTH, MAX_REFRESH_PIXELS);

    for (let chunkWidth = 1; chunkWidth <= maxW; chunkWidth++) {
        const chunkHeight = Math.min(
            height,
            MAX_REFRESH_HEIGHT,
            Math.floor(MAX_REFRESH_PIXELS / chunkWidth)
        );

        if (chunkHeight < 1) {
            continue;
        }

        const cols = Math.ceil(width / chunkWidth);
        const rows = Math.ceil(height / chunkHeight);
        const count = cols * rows;
        const area = chunkWidth * chunkHeight;

        if (!best ||
            count < best.count ||
            (count === best.count && area > best.area) ||
            (count === best.count && area === best.area && cols < best.cols)) {
            best = {
                width: chunkWidth,
                height: chunkHeight,
                count,
                area,
                cols
            };
        }
    }

    if (!best) {
        throw new Error(`cannot split LCD refresh area ${width}x${height}`);
    }

    return best;
}

function splitHardwareRegion(hwRegion) {
    const shape = chooseChunkShape(hwRegion.rect.width, hwRegion.rect.height);
    const chunks = [];

    for (let y = 0; y < hwRegion.rect.height; y += shape.height) {
        const height = Math.min(shape.height, hwRegion.rect.height - y);

        for (let x = 0; x < hwRegion.rect.width; x += shape.width) {
            const width = Math.min(shape.width, hwRegion.rect.width - x);
            const data = new Uint16Array(width * height);
            let dst = 0;

            for (let row = 0; row < height; row++) {
                const srcStart =
                    (y + row) * hwRegion.rect.width + x;
                data.set(
                    hwRegion.data.subarray(srcStart, srcStart + width),
                    dst
                );
                dst += width;
            }

            chunks.push({
                x: hwRegion.rect.x + x,
                y: hwRegion.rect.y + y,
                width,
                height,
                data
            });
        }
    }

    return chunks;
}

function scheduleDraw() {
    if (drawing || drawTimer || !pendingRect || !handle) {
        return;
    }

    const delay = Math.max(0, retryNotBefore - Date.now());

    drawTimer = setTimeout(() => {
        drawTimer = null;
        void pumpDraw();
    }, delay);
}

function requeueRect(rect, forceFull) {
    pendingRect = mergeRect(pendingRect, rect);
    pendingFull = pendingFull || forceFull;
}

async function sendPartial(rect) {
    const hwRegion = logicalRectToHardware(rect);
    const chunks = splitHardwareRegion(hwRegion);

    for (const chunk of chunks) {
        /*
         * If a whole-page redraw arrived while this partial update was in
         * flight, stop after the current HID write. The upcoming redraw will
         * replace the complete panel and there is no value painting stale
         * chunks first.
         */
        if (pendingFull) {
            return { chunks: 0, supersededByFull: true };
        }

        await lcd.refresh(
            handle,
            chunk.x,
            chunk.y,
            chunk.width,
            chunk.height,
            { data: chunk.data }
        );

        statsChunks++;
    }

    return { chunks: chunks.length, supersededByFull: false };
}

async function pumpDraw() {
    if (drawing || !pendingRect || !handle || !logicalHasData) {
        return;
    }

    const rect = pendingRect;
    const requestedFull = pendingFull;
    pendingRect = null;
    pendingFull = false;
    drawing = true;

    const useFull = shouldUseFullRedraw(rect, requestedFull);
    const startedAt = Date.now();
    let transferred = false;

    try {
        if (useFull) {
            await lcd.redraw(handle, fullHardwareImage());
            statsFull++;
            transferred = true;
        } else {
            const result = await sendPartial(rect);

            if (!result.supersededByFull) {
                statsPartial++;
                transferred = result.chunks > 0;
            }
        }

        retryNotBefore = 0;
    } catch (err) {
        statsErrors++;
        console.error(
            `LCD ${useFull ? 'redraw' : 'refresh'} error for ` +
            `${rect.x},${rect.y} ${rect.width}x${rect.height}:`,
            err
        );

        /*
         * Reconstructing logicalPixels is independent of physical success.
         * Requeue the failed logical area so the latest image is retried.
         */
        requeueRect(rect, useFull);
        retryNotBefore = Date.now() + ERROR_BACKOFF_MS;
    } finally {
        if (transferred || useFull) {
            statsLastTransferMs = Date.now() - startedAt;
            if (statsLastTransferMs > statsMaxTransferMs) {
                statsMaxTransferMs = statsLastTransferMs;
            }
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
        'S1 LCD opened; LVGL invalid-area refresh, widget updates use LCD_REFRESH, ' +
        'large/page updates use LCD_REDRAW'
    );

    await lcd.set_orientation(handle, true);

    process.stdin.on('data', chunk => {
        rx = Buffer.concat([rx, chunk]);

        try {
            parsePipeMessages();
        } catch (err) {
            console.error('S1 LCD pipe protocol error:', err);
            rx = Buffer.alloc(0);
        }
    });

    process.stdin.resume();

    setInterval(async () => {
        if (!drawing && !drawTimer && !pendingRect && handle) {
            try {
                await lcd.heartbeat(handle);
            } catch (_) {
                /* Firmware heartbeat failures are intentionally non-fatal. */
            }
        }
    }, HEARTBEAT_INTERVAL_MS);

    setInterval(() => {
        console.error(
            `LCD stats: messages=${statsMessages} merged=${statsMerged} ` +
            `full=${statsFull} partial=${statsPartial} chunks=${statsChunks} ` +
            `errors=${statsErrors} transfer=${statsLastTransferMs}ms ` +
            `max=${statsMaxTransferMs}ms`
        );

        statsMessages = 0;
        statsMerged = 0;
        statsFull = 0;
        statsPartial = 0;
        statsChunks = 0;
        statsErrors = 0;
        statsMaxTransferMs = statsLastTransferMs;
    }, STATS_INTERVAL_MS);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
