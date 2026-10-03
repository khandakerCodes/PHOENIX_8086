// SPDX-License-Identifier: MIT
/**
 * The JavaScript decoder must agree with the Python one. The fixture
 * is the raw serial output of a real kernel boot (boot.bin) and the
 * messages bridge/protocol.py decoded from it (boot.json).
 */
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const Protocol = require('../protocol.js');

const raw = fs.readFileSync(path.join(__dirname, 'fixtures', 'boot.bin'));
const expected = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'boot.json'), 'utf8'));

test('decodes a real boot to exactly the messages the Python decoder produced', () => {
    const decoder = new Protocol.Decoder();
    const messages = decoder.feed(raw);
    assert.strictEqual(decoder.frames, expected.frames);
    assert.strictEqual(decoder.badFrames, expected.bad_frames);
    assert.strictEqual(messages.length, expected.messages.length);
    messages.forEach((message, i) => assert.deepStrictEqual(message, expected.messages[i], 'message ' + i));
});

test('gives the same result when the bytes arrive one at a time', () => {
    const decoder = new Protocol.Decoder();
    let messages = [];
    for (const byte of raw) {
        messages = messages.concat(decoder.feed([byte]));
    }
    assert.deepStrictEqual(messages, expected.messages);
});

test('every record type the kernel sends at boot is covered', () => {
    const types = new Set(expected.messages.map((m) => m.type));
    for (const type of ['HELLO', 'BOOT_STAGE', 'THREAD_CREATE', 'THREAD_STATE', 'CONTEXT_SWITCH',
                        'COUNTERS', 'MEMORY', 'CONSOLE', 'THREAD_STATS']) {
        assert.ok(types.has(type), type);
    }
});

test('console text is decoded as code page 437', () => {
    const last = expected.messages[expected.messages.length - 1];
    assert.strictEqual(last.text, 'äöüß£A');
    const decoder = new Protocol.Decoder();
    const messages = decoder.feed(raw);
    assert.strictEqual(messages[messages.length - 1].text, 'äöüß£A');
});

test('CRC matches the reference check value', () => {
    const bytes = Uint8Array.from(Buffer.from('123456789'));
    assert.strictEqual(Protocol.crc16(bytes, bytes.length), 0x29B1);
});
