'use strict';

const node_hid = require('node-hid');
const lcd = require('./legacy/lcd_device');

node_hid.setDriverType('libusb');

const WIDTH = 170;
const HEIGHT = 320;
const PIXELS = WIDTH * HEIGHT;
const FRAME_BYTES = PIXELS * 2;

let handle = null;
let rx = Buffer.alloc(0);
let drawing = false;
let pendingFrame = null;

function frameToImage(frame) {
    const pixels = new Uint16Array(PIXELS);

    for (let i = 0; i < PIXELS; i++) {
        pixels[i] = frame.readUInt16LE(i * 2);
    }

    return { data: pixels };
}

async function drawFrame(frame) {
    if (drawing) {
        pendingFrame = Buffer.from(frame);
        return;
    }

    drawing = true;

    try {
        let current = Buffer.from(frame);

        while (current) {
            pendingFrame = null;
            await lcd.redraw(handle, frameToImage(current));
            current = pendingFrame;
        }
    } catch (err) {
        console.error('LCD redraw error:', err);
    } finally {
        drawing = false;
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

    console.error(
        `S1 LCD found: ${device.path}, interface ${device.interface}`
    );

    handle = await node_hid.HIDAsync.open(device.path);

    console.error('S1 LCD opened');

    await lcd.set_orientation(handle, true);

    process.stdin.on('data', chunk => {
        rx = Buffer.concat([rx, chunk]);

        while (rx.length >= FRAME_BYTES) {
            const frame = Buffer.from(rx.subarray(0, FRAME_BYTES));
            rx = rx.subarray(FRAME_BYTES);
            drawFrame(frame);
        }
    });

    process.stdin.resume();

    setInterval(async () => {
        if (!drawing && handle) {
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
