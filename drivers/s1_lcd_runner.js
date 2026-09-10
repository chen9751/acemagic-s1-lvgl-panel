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
 * Keep the hardware side conservative: at most ~15 FPS, and while one
 * frame is being transmitted retain only the newest queued framebuffer.
 */
const MAX_FPS = 15;
const MIN_FRAME_INTERVAL_MS = Math.ceil(1000 / MAX_FPS);
const ERROR_BACKOFF_MS = 250;

let handle = null;
let rx = Buffer.alloc(0);
let drawing = false;
let pendingFrame = null;
let drawTimer = null;
let lastDrawStartedAt = 0;
let retryNotBefore = 0;

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
     * This mapping is already verified on real hardware; do not alter it
     * as part of redraw scheduling changes.
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
        retryNotBefore
    );
    const delay = Math.max(0, nextAllowedAt - now);

    drawTimer = setTimeout(() => {
        drawTimer = null;
        void pumpDraw();
    }, delay);
}

function queueFrame(frame) {
    /*
     * Latest-frame-wins queue:
     * replace any frame that has not started transmitting yet.
     */
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
        await lcd.redraw(
            handle,
            frameToImage(frame)
        );
        retryNotBefore = 0;
    } catch (err) {
        console.error('LCD redraw error:', err);

        /*
         * A failed HID transfer should not trigger a tight retry loop.
         * New LVGL frames may continue replacing pendingFrame during this
         * short backoff; once it expires only the newest frame is sent.
         */
        retryNotBefore = Date.now() + ERROR_BACKOFF_MS;
    } finally {
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
        throw new Error(
            'S1 LCD 04d9:fd01 interface 1 not found'
        );
    }

    console.error(
        `S1 LCD found: ${device.path}, interface ${device.interface}`
    );

    handle = await node_hid.HIDAsync.open(
        device.path
    );

    console.error(`S1 LCD opened; redraw limit ${MAX_FPS} FPS`);

    await lcd.set_orientation(
        handle,
        true
    );

    process.stdin.on('data', chunk => {
        rx = Buffer.concat([
            rx,
            chunk
        ]);

        while (rx.length >= FRAME_BYTES) {
            const frame = Buffer.from(
                rx.subarray(
                    0,
                    FRAME_BYTES
                )
            );

            rx = rx.subarray(
                FRAME_BYTES
            );

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
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
