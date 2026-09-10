'use strict';

const node_hid = require('node-hid');
const lcd = require('./legacy/lcd_device');

node_hid.setDriverType('libusb');

const WIDTH = 170;
const HEIGHT = 320;
const PIXELS = WIDTH * HEIGHT;
const HW_WIDTH = HEIGHT;

const PIPE_HEADER_SIZE = 18;
const PIPE_MAGIC = Buffer.from('S1UP');
const PIPE_VERSION = 1;
const PIPE_PARTIAL = 1;
const PIPE_FULL = 2;

const MAX_REFRESH_PIXELS = 2048;
const MAX_REFRESH_WIDTH = 255;
const MAX_REFRESH_HEIGHT = 255;
const DIRTY_COALESCE_MS = 4;
const ERROR_BACKOFF_MS = 250;
const HEARTBEAT_INTERVAL_MS = 5000;
const STATS_INTERVAL_MS = 5000;

let handle = null;
let rx = Buffer.alloc(0);
let drawing = false;
let drawTimer = null;
let retryNotBefore = 0;
let initialRedrawDone = false;

/* Latest logical 170x320 RGB565 image reconstructed from LVGL flushes. */
const logicalPixels = new Uint16Array(PIXELS);
let logicalHasData = false;

/*
 * Keep several dirty rectangles instead of collapsing everything into one
 * bounding box. Two rectangles are merged only when doing so does not
 * increase the estimated number of LCD_REFRESH HID commands.
 */
let dirtyRects = [];
let initialFullSeen = false;

let statsMessages = 0;
let statsMerged = 0;
let statsSuperseded = 0;
let statsInitialFull = 0;
let statsPartialBatches = 0;
let statsRects = 0;
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

function rectRight(rect) {
    return rect.x + rect.width - 1;
}

function rectBottom(rect) {
    return rect.y + rect.height - 1;
}

function mergeRect(a, b) {
    const x1 = Math.min(a.x, b.x);
    const y1 = Math.min(a.y, b.y);
    const x2 = Math.max(rectRight(a), rectRight(b));
    const y2 = Math.max(rectBottom(a), rectBottom(b));

    return {
        x: x1,
        y: y1,
        width: x2 - x1 + 1,
        height: y2 - y1 + 1
    };
}

function rectsTouchOrOverlap(a, b) {
    return !(
        rectRight(a) + 1 < b.x ||
        rectRight(b) + 1 < a.x ||
        rectBottom(a) + 1 < b.y ||
        rectBottom(b) + 1 < a.y
    );
}

function rectContains(outer, inner) {
    return outer.x <= inner.x &&
        outer.y <= inner.y &&
        rectRight(outer) >= rectRight(inner) &&
        rectBottom(outer) >= rectBottom(inner);
}

function pendingSupersedes(rect) {
    return dirtyRects.some(pending => rectContains(pending, rect));
}

function isFullLogicalRect(rect) {
    return rect.x === 0 &&
        rect.y === 0 &&
        rect.width === WIDTH &&
        rect.height === HEIGHT;
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

function estimateRefreshChunks(rect) {
    /* Portrait logical rect rotates to hardware width=height, height=width. */
    return chooseChunkShape(rect.height, rect.width).count;
}

function addDirtyRect(rect) {
    let incoming = { ...rect };

    for (;;) {
        let merged = false;

        for (let i = 0; i < dirtyRects.length; i++) {
            const current = dirtyRects[i];

            if (!rectsTouchOrOverlap(current, incoming)) {
                continue;
            }

            const combined = mergeRect(current, incoming);
            const separateCost =
                estimateRefreshChunks(current) + estimateRefreshChunks(incoming);
            const combinedCost = estimateRefreshChunks(combined);

            if (combinedCost <= separateCost) {
                dirtyRects.splice(i, 1);
                incoming = combined;
                statsMerged++;
                merged = true;
                break;
            }
        }

        if (!merged) {
            break;
        }
    }

    dirtyRects.push(incoming);
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

        if (payloadBytes !== expectedBytes || payloadBytes > PIXELS * 2) {
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

        if (!initialRedrawDone &&
            (type === PIPE_FULL || isFullLogicalRect(rect))) {
            initialFullSeen = true;
        }

        addDirtyRect(rect);
        scheduleDraw();
    }
}

function fullHardwareImage() {
    const pixels = new Uint16Array(PIXELS);

    /* Verified mapping: LVGL (x,y) -> S1 hardware (y, 169-x). */
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
                const srcStart = (y + row) * hwRegion.rect.width + x;
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
    if (drawing || drawTimer || dirtyRects.length === 0 || !handle) {
        return;
    }

    const delay = Math.max(
        DIRTY_COALESCE_MS,
        retryNotBefore - Date.now()
    );

    drawTimer = setTimeout(() => {
        drawTimer = null;
        void pumpDraw();
    }, Math.max(0, delay));
}

async function sendPartial(rect) {
    const hwRegion = logicalRectToHardware(rect);
    const chunks = splitHardwareRegion(hwRegion);

    for (let i = 0; i < chunks.length; i++) {
        const chunk = chunks[i];

        await lcd.refresh(
            handle,
            chunk.x,
            chunk.y,
            chunk.width,
            chunk.height,
            { data: chunk.data }
        );
        statsChunks++;

        /*
         * Continuous LVGL animations (for example a long scrolling song
         * title) can invalidate the same large area again while an older
         * version is still being transferred. If the pending newest dirty
         * rectangle fully covers this one, the unsent chunks are already
         * stale. Stop this transfer and let the newest framebuffer replace it.
         * This is safe because the next pending update covers every pixel of
         * the abandoned logical rectangle.
         */
        if (i + 1 < chunks.length && pendingSupersedes(rect)) {
            statsSuperseded++;
            return { chunks: i + 1, superseded: true };
        }
    }

    return { chunks: chunks.length, superseded: false };
}

async function pumpDraw() {
    if (drawing || dirtyRects.length === 0 || !handle || !logicalHasData) {
        return;
    }

    drawing = true;
    const startedAt = Date.now();
    let transferred = false;

    try {
        /*
         * REDRAW is deliberately kept out of the normal interaction path.
         * It is used once for the initial complete framebuffer only.
         */
        if (!initialRedrawDone && initialFullSeen) {
            await lcd.redraw(handle, fullHardwareImage());
            initialRedrawDone = true;
            initialFullSeen = false;
            dirtyRects = [];
            statsInitialFull++;
            transferred = true;
        } else {
            /*
             * Snapshot current dirty rectangles. New LVGL updates arriving
             * while HID is busy stay queued and will be rendered afterwards
             * from the newest logical framebuffer.
             */
            const batch = dirtyRects;
            dirtyRects = [];

            /* Smaller regions first make button/focus feedback visible ASAP. */
            batch.sort((a, b) =>
                (a.width * a.height) - (b.width * b.height)
            );

            for (let i = 0; i < batch.length; i++) {
                const rect = batch[i];

                try {
                    const result = await sendPartial(rect);
                    statsRects++;
                    transferred = result.chunks > 0 || transferred;
                } catch (err) {
                    /* Requeue this and all unprocessed regions using latest data. */
                    addDirtyRect(rect);
                    for (let j = i + 1; j < batch.length; j++) {
                        addDirtyRect(batch[j]);
                    }
                    throw err;
                }
            }

            statsPartialBatches++;
        }

        retryNotBefore = 0;
    } catch (err) {
        statsErrors++;
        retryNotBefore = Date.now() + ERROR_BACKOFF_MS;
        console.error('LCD refresh scheduler error:', err);
    } finally {
        if (transferred) {
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
        'S1 LCD opened; REDRAW initial sync only, normal UI uses preemptive LCD_REFRESH'
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
        if (!drawing && !drawTimer && dirtyRects.length === 0 && handle) {
            try {
                await lcd.heartbeat(handle);
            } catch (_) {
                /* Heartbeat failures are intentionally non-fatal. */
            }
        }
    }, HEARTBEAT_INTERVAL_MS);

    setInterval(() => {
        console.error(
            `LCD stats: messages=${statsMessages} merged=${statsMerged} ` +
            `superseded=${statsSuperseded} initialFull=${statsInitialFull} ` +
            `batches=${statsPartialBatches} rects=${statsRects} ` +
            `chunks=${statsChunks} errors=${statsErrors} ` +
            `transfer=${statsLastTransferMs}ms max=${statsMaxTransferMs}ms ` +
            `pending=${dirtyRects.length}`
        );

        statsMessages = 0;
        statsMerged = 0;
        statsSuperseded = 0;
        statsInitialFull = 0;
        statsPartialBatches = 0;
        statsRects = 0;
        statsChunks = 0;
        statsErrors = 0;
        statsMaxTransferMs = statsLastTransferMs;
    }, STATS_INTERVAL_MS);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});