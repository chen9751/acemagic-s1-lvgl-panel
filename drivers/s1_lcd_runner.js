'use strict';

const node_hid = require('node-hid');
const lcd = require('./legacy/lcd_device');

node_hid.setDriverType('libusb');

const WIDTH = 170;
const HEIGHT = 320;
const PIXELS = WIDTH * HEIGHT;
const FRAME_BYTES = PIXELS * 2;

/*
 * The S1 LCD redraw protocol sends a full framebuffer as 27 HID writes.
 * Keep the hardware side conservative and never transmit frames back-to-back.
 */
const MAX_FPS = 15;
const MIN_FRAME_INTERVAL_MS = Math.ceil(1000 / MAX_FPS);
const POST_REDRAW_COOLDOWN_MS = 20;
const ERROR_BACKOFF_MS = 250;
const STATS_INTERVAL_MS = 5000;

let handle = null;
let rx = Buffer.alloc(0);
let drawing = false;
let pendingFrame = null;
let drawTimer = null;
let lastDrawStartedAt = 0;
let lastDrawFinishedAt = 0;
let retryNotBefore = 0;
let lastSuccessfulFrame = null;

let statsReceived = 0;
let statsSent = 0;
let statsDuplicate = 0;
let statsReplaced = 0;
let statsErrors = 0;
let statsLastDrawMs = 0;
let statsMaxDrawMs = 0;

function frameToImage(frame) {
    const src = new Uint16Array(PIXELS);
    const pixels = new Uint16Array(PIXELS);

    for (let i = 0; i < PIXELS; i++) {
        src[i] = frame.readUInt16LE(i * 2);
    }

    /*
     * LVGL source:
     *   170 x 320 portrait
     *
     * Reorder into 320 x 170 scan order required by the S1 LCD.
     * This mapping is already verified on real hardware; do not alter it.
     */
    for (let y = 0; y < HEIGHT; y++) {
        for (let x = 0; x < WIDTH; x++) {
            const srcIndex = y * WIDTH + x;
            const dstX = y;
            const dstY = WIDTH - 1 - x;
            const dstIndex = dstY * HEIGHT + dstX;
            pixels[dstIndex] = src[srcIndex];
        }
    }

    return { data: pixels };
}

function scheduleDraw() {
    if (drawing || drawTimer || !pendingFrame || !handle) {
        return;
    }

    const now = Date.now();
    const nextAllowedAt = Math.max(
        lastDrawStartedAt + MIN_FRAME_INTERVAL_MS,
        lastDrawFinishedAt + POST_REDRAW_COOLDOWN_MS,
        retryNotBefore
    );
    const delay = Math.max(0, nextAllowedAt - now);

    drawTimer = setTimeout(() => {
        drawTimer = null;
        void pumpDraw();
    }, delay);
}

function queueFrame(frame) {
    statsReceived++;

    /* Do not resend a framebuffer already displayed successfully. */
    if (lastSuccessfulFrame && frame.equals(lastSuccessfulFrame)) {
        statsDuplicate++;
        return;
    }

    /* If the exact same framebuffer is already waiting, keep one copy only. */
    if (pendingFrame && frame.equals(pendingFrame)) {
        statsDuplicate++;
        return;
    }

    if (pendingFrame) {
        statsReplaced++;
    }

    /* Latest-frame-wins queue. */
    pendingFrame = Buffer.from(frame);
    scheduleDraw();
}

async function pumpDraw() {
    if (drawing || !pendingFrame || !handle) {
        return;
    }

    const frame = pendingFrame;
    pendingFrame = null;
    drawing = true;
    lastDrawStartedAt = Date.now();

    try {
        await lcd.redraw(handle, frameToImage(frame));
        lastSuccessfulFrame = frame;
        retryNotBefore = 0;
        statsSent++;
    } catch (err) {
        statsErrors++;
        console.error('LCD redraw error:', err);
        retryNotBefore = Date.now() + ERROR_BACKOFF_MS;
    } finally {
        lastDrawFinishedAt = Date.now();
        statsLastDrawMs = lastDrawFinishedAt - lastDrawStartedAt;
        if (statsLastDrawMs > statsMaxDrawMs) {
            statsMaxDrawMs = statsLastDrawMs;
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
        `S1 LCD opened; max ${MAX_FPS} FPS, ${POST_REDRAW_COOLDOWN_MS}ms post-redraw cooldown`
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
        if (!drawing && !drawTimer && handle) {
            try {
                await lcd.heartbeat(handle);
            } catch (_) {
            }
        }
    }, 5000);

    setInterval(() => {
        console.error(
            `LCD stats: rx=${statsReceived} sent=${statsSent} duplicate=${statsDuplicate} ` +
            `replaced=${statsReplaced} errors=${statsErrors} redraw=${statsLastDrawMs}ms max=${statsMaxDrawMs}ms`
        );

        statsReceived = 0;
        statsSent = 0;
        statsDuplicate = 0;
        statsReplaced = 0;
        statsErrors = 0;
        statsMaxDrawMs = statsLastDrawMs;
    }, STATS_INTERVAL_MS);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
