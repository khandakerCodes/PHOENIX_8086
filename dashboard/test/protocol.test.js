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

test('a reboot restarts the numbering without counting a loss', () => {
    const frame = (type, seq, payload) => {
        const content = [1, type, seq, 0, 0, 0, 0, ...payload];
        const crc = Protocol.crc16(Uint8Array.from(content), content.length);
        return [0x7E, ...content, crc & 0xFF, crc >> 8, 0x7E];
    };
    const decoder = new Protocol.Decoder();
    decoder.feed(frame(9, 200, [0x41]));     // CONSOLE, late in a session
    decoder.feed(frame(1, 0, [1]));          // BOOT_STAGE 1: the machine rebooted
    decoder.feed(frame(1, 1, [2]));
    assert.strictEqual(decoder.lostFrames, 0);
    decoder.feed(frame(1, 5, [3]));
    assert.strictEqual(decoder.lostFrames, 3);
});

test('decodes THREAD_FAULT and the stack peaks added to THREAD_STATS', () => {
    const body = (type, payload) => {
        const content = [1, type, 0, 0, 0, 0, 0, ...payload];
        const crc = Protocol.crc16(Uint8Array.from(content), content.length);
        return Uint8Array.from([...content, crc & 0xFF, crc >> 8]);
    };
    const fault = Protocol.decodeFrame(body(0x0D, [4, 1, 0x12, 0x08]));
    assert.deepStrictEqual([fault.type, fault.tid, fault.kind, fault.detail],
                           ['THREAD_FAULT', 4, 'program_stack', 0x0812]);

    const name = [...Buffer.from('rogue'), 0, 0, 0, 0, 0, 0, 0];
    const base = [3, 0, 5, 7, 0, 0, 0, 0x00, 0x80, 0x00, 0x78, 0x00, 0x08, ...name];
    const stats = Protocol.decodeFrame(body(0x0C, [...base, 0x38, 0x01, 0xBC, 0x07]));
    assert.deepStrictEqual([stats.stack_peak, stats.program_stack_peak], [312, 1980]);
    assert.strictEqual(Protocol.decodeFrame(body(0x0C, base)).stack_peak, undefined);
});

test('CONTEXT_SWITCH from a newer kernel carries the position within the tick', () => {
    const content = [1, 5, 0, 0, 0, 0, 0, 0, 1, ...new Array(22).fill(0), 0x4E, 0x17];
    const crc = Protocol.crc16(Uint8Array.from(content), content.length);
    const message = Protocol.decodeFrame(Uint8Array.from([...content, crc & 0xFF, crc >> 8]));
    assert.strictEqual(message.sub_tick, 5966);
});
