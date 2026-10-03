#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — In-browser demo engine test
 *
 * Boots the built site's floppy image in v86 under Node, exactly the
 * emulator and files the demo page uses, decodes the serial stream
 * with dashboard/protocol.js, feeds dashboard/model.js, and types a
 * command through the emulated serial port.
 *
 * It tests the engine of the demo. It does not open a browser, so it
 * cannot tell whether the page looks right.
 *
 * Usage: node tools/test_browser_demo.mjs site/   (after tools/build_site.sh)
 * Needs the v86 package: npm install v86  (NODE_PATH may point at it)
 */

import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const site = path.resolve(process.argv[2] || 'site');
const Protocol = require('../dashboard/protocol.js');
const Model = require('../dashboard/model.js');

let V86;
try {
    ({ V86 } = await import('v86'));
} catch (error) {
    const fallback = process.env.NODE_PATH && path.join(process.env.NODE_PATH, 'v86', 'build', 'libv86.mjs');
    if (!fallback || !fs.existsSync(fallback)) {
        console.error('The v86 package is not installed (npm install v86).');
        process.exit(1);
    }
    ({ V86 } = await import(fallback));
}

const file = (name) => {
    const data = fs.readFileSync(path.join(site, 'emulator', name));
    return { buffer: data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) };
};

const emulator = new V86({
    wasm_path: path.join(site, 'emulator', 'v86.wasm'),
    bios: file('seabios.bin'),
    vga_bios: file('vgabios.bin'),
    fda: file('phoenix8086.img'),
    boot_order: 0x321,
    memory_size: 2 * 1024 * 1024,
    vga_memory_size: 2 * 1024 * 1024,
    autostart: true,
});

const decoder = new Protocol.Decoder();
const model = Model.create();
Model.apply(model, { type: 'BRIDGE', mode: 'emulator', serial: true, reset: true });
emulator.add_listener('serial0-output-byte', (byte) => {
    decoder.feed([byte]).forEach((message) => Model.apply(model, message));
});

const consoleText = () => model.console.join('\n');
const failures = [];
const check = (name, ok, detail = '') => {
    console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${name}`);
    if (!ok) failures.push(`${name}: ${detail}`);
};

async function waitFor(pattern, seconds) {
    const deadline = Date.now() + seconds * 1000;
    while (Date.now() < deadline) {
        if (pattern.test(consoleText())) return true;
        await new Promise((resolve) => setTimeout(resolve, 200));
    }
    return false;
}

check('kernel boots to the shell in v86', await waitFor(/boot complete[\s\S]*phoenix> /, 60),
      consoleText().slice(-200));

emulator.serial0_send('selftest\r');
check('typed command runs: self-tests pass', await waitFor(/selftest: \d+ passed, 0 failed/, 60),
      consoleText().slice(-200));

emulator.serial0_send('run primes.bin\r');
check('a program loads from the floppy image', await waitFor(/largest 997/, 60), consoleText().slice(-200));

check('telemetry decoded cleanly',
      decoder.frames > 100 && decoder.badFrames === 0 && decoder.lostFrames === 0,
      `frames=${decoder.frames} bad=${decoder.badFrames} lost=${decoder.lostFrames}`);
const names = Model.liveThreads(model).map((t) => t.name);
check('dashboard model has the threads', ['idle', 'shell', 'telemetry'].every((n) => names.includes(n)),
      names.join(','));
check('dashboard model has the machine layout and a schedule',
      Model.memoryRegions(model).length === 8 && Model.schedule(model, 300).blocks.length > 0);
check('no fault', model.fault === null, JSON.stringify(model.fault));

if (failures.length) {
    console.log(`Browser demo test FAILED (${failures.length}):`);
    failures.forEach((failure) => console.log(`  - ${failure.slice(0, 400)}`));
    process.exit(1);
}
console.log('  Browser demo engine test passed');
process.exit(0);
