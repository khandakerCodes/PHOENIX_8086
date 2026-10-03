// SPDX-License-Identifier: MIT
/**
 * Tests for the dashboard model. Run: node --test dashboard/test/*.test.js
 *
 * The fixture is a real kernel session recorded by tools/record_session.py:
 * boot, three demo threads, ps, ipc, syscall, memory, panic.
 */
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const Model = require('../model.js');

function loadCapture() {
    const lines = fs.readFileSync(path.join(__dirname, 'fixtures', 'session.jsonl'), 'utf8')
        .split('\n').filter(Boolean);
    assert.strictEqual(JSON.parse(lines[0]).capture, 1);
    return lines.slice(1).map((line) => JSON.parse(line).msg);
}

function replay(messages) {
    const model = Model.create();
    messages.forEach((message) => Model.apply(model, message));
    return model;
}

const capture = loadCapture();

test('an empty model shows nothing', () => {
    const model = Model.create();
    assert.strictEqual(model.received, 0);
    assert.deepStrictEqual(Model.liveThreads(model), []);
    assert.deepStrictEqual(Model.memoryRegions(model), []);
    assert.deepStrictEqual(Model.schedule(model, 300).blocks, []);
    assert.strictEqual(model.counters, null);
    assert.strictEqual(model.irqRate, null);
    assert.strictEqual(model.lastSwitch, null);
});

test('replaying the capture twice gives the same state', () => {
    assert.deepStrictEqual(replay(capture), replay(capture));
});

test('boot stages are observed, not inferred, when seen from the start', () => {
    const model = replay(capture);
    assert.strictEqual(model.bootStage, Model.BOOT_COMPLETE);
    assert.strictEqual(model.bootInferred, false);
});

test('joining late infers a finished boot and says so', () => {
    const late = capture.slice(capture.findIndex((m) => m.type === 'THREAD_STATS'))
        .filter((m) => m.type !== 'BOOT_STAGE');
    const model = replay(late);
    assert.strictEqual(model.bootStage, Model.BOOT_COMPLETE);
    assert.strictEqual(model.bootInferred, true);
});

test('counters match the last COUNTERS record exactly', () => {
    const model = replay(capture);
    const last = capture.filter((m) => m.type === 'COUNTERS').pop();
    assert.strictEqual(model.counters.contextSwitches, last.context_switches);
    assert.strictEqual(model.counters.timer, last.timer);
    assert.strictEqual(model.counters.drops, 0);
    assert.strictEqual(model.irqRate, 100);
});

test('threads: survivors are listed, exited threads are not', () => {
    const model = replay(capture);
    const names = Model.liveThreads(model).map((t) => t.name);
    assert.deepStrictEqual(names, ['idle', 'shell', 'telemetry']);
    const exits = capture.filter((m) => m.type === 'THREAD_EXIT').length;
    assert.strictEqual(exits, 6);
});

test('schedule: blocks tile the window and shares add up', () => {
    const model = replay(capture);
    const view = Model.schedule(model, 300);
    assert.ok(view.blocks.length > 5);
    for (let i = 1; i < view.blocks.length; i++) {
        assert.strictEqual(view.blocks[i].start, view.blocks[i - 1].end);
    }
    const total = view.shares.reduce((sum, share) => sum + share.ticks, 0);
    assert.strictEqual(total, view.covered);
    assert.strictEqual(view.blocks[view.blocks.length - 1].end, model.tick);
});

test('schedule: the three demo threads share the CPU while they run', () => {
    const upTo = capture.findIndex((m) => m.type === 'THREAD_EXIT');
    const model = replay(capture.slice(0, upTo));
    const view = Model.schedule(model, 100);
    const demo = view.shares.filter((share) => /^demo-/.test(model.threads[share.tid].name));
    assert.strictEqual(demo.length, 3);
    assert.ok(demo.reduce((sum, share) => sum + share.percent, 0) >= 90);
});

test('one segment is recorded per context switch', () => {
    const model = replay(capture);
    const switches = capture.filter((m) => m.type === 'CONTEXT_SWITCH').length;
    assert.strictEqual(model.segments.length, switches);
});

test('registers come from the last switch and diffs are against the one before', () => {
    const switches = capture.filter((m) => m.type === 'CONTEXT_SWITCH');
    const last = switches[switches.length - 1];
    const before = switches[switches.length - 2];
    const model = replay(capture);
    assert.deepStrictEqual(model.lastSwitch.regs, last.regs);
    assert.strictEqual(model.lastSwitch.to, last.to_tid);
    Object.keys(last.regs).forEach((name) => {
        assert.strictEqual(model.lastSwitch.changed[name], last.regs[name] !== before.regs[name]);
    });
});

test('memory map comes from HELLO and MEMORY', () => {
    const model = replay(capture);
    const regions = Model.memoryRegions(model);
    const byKey = Object.fromEntries(regions.map((r) => [r.key, r]));
    assert.strictEqual(byKey.kernel.start, 0x10000);
    assert.strictEqual(byKey.threads.start, 0x20000);
    assert.strictEqual(byKey.far.start, 0x30000);
    assert.strictEqual(byKey.heap.total, model.memory.heapFree + model.memory.heapUsed);
    for (let i = 1; i < regions.length; i++) {
        assert.ok(regions[i].start >= regions[i - 1].end, 'regions must not overlap');
    }
});

test('console text is reassembled, including backspaces', () => {
    const model = replay(capture);
    const text = model.console.join('\n');
    assert.ok(text.includes('Phoenix-8086 boot complete.'));
    assert.ok(text.includes('=== Thread List ==='));
    assert.ok(text.includes('[ipc done]'));

    const edited = Model.create();
    Model.apply(edited, { type: 'CONSOLE', tick: 1, text: 'helpp\b\nok' });
    assert.deepStrictEqual(edited.console, ['help', 'ok']);
});

test('the panic is reported with its reason and registers', () => {
    const model = replay(capture);
    assert.strictEqual(model.fault.reason, 'User-triggered panic via shell');
    assert.strictEqual(model.threads[model.fault.tid].name, 'shell');
    assert.strictEqual(model.fault.regs.cs, 0x1000);
    assert.strictEqual(model.events[0].kind === 'fault' || model.events.some((e) => e.kind === 'fault'), true);
});

test('system calls are counted and named', () => {
    const model = replay(capture);
    assert.strictEqual(model.syscalls, capture.filter((m) => m.type === 'SYSCALL').length);
    const upTo = capture.map((m) => m.type).lastIndexOf('SYSCALL') + 1;
    const during = replay(capture.slice(0, upTo));
    assert.ok(during.events.some((e) => e.kind === 'syscall' && /INT 80h thread_exit/.test(e.text)));
});

test('stack usage is computed from saved SP and bounds', () => {
    const model = replay(capture);
    const shell = Model.liveThreads(model).find((t) => t.name === 'shell');
    const used = Model.stackUsed(shell);
    assert.ok(used > 0 && used < shell.stackSize);
    assert.strictEqual(Model.stackUsed({ sp: null, stackBase: 1, stackSize: 2 }), null);
});

test('a bridge reset clears everything', () => {
    const model = replay(capture);
    Model.apply(model, { type: 'BRIDGE', mode: 'live', serial: true, reset: true });
    assert.strictEqual(model.received, 0);
    assert.deepStrictEqual(model.threads, {});
    assert.strictEqual(model.fault, null);
    assert.strictEqual(model.link.mode, 'live');
});

test('dropped records are surfaced as an event', () => {
    const model = Model.create();
    Model.apply(model, { type: 'COUNTERS', tick: 10, timer: 10, keyboard: 0, syscall: 0, context_switches: 1, drops: 0 });
    Model.apply(model, { type: 'COUNTERS', tick: 30, timer: 30, keyboard: 0, syscall: 0, context_switches: 2, drops: 4 });
    assert.strictEqual(model.counters.drops, 4);
    assert.ok(model.events.some((e) => /dropped 4 telemetry records/.test(e.text)));
});

test('unknown and malformed messages are ignored', () => {
    const model = Model.create();
    Model.apply(model, null);
    Model.apply(model, { type: 'NOT_A_TYPE', tick: 5 });
    Model.apply(model, { tick: 5 });
    assert.strictEqual(model.received, 0);
    assert.strictEqual(model.tick, 0);
});
