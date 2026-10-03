// SPDX-License-Identifier: MIT
/**
 * Runs the dashboard's page code against a stand-in DOM (fakedom.js)
 * and a recorded kernel session. Catches missing elements, runtime
 * errors and wrong content. It cannot check how the page looks.
 */
'use strict';

const test = require('node:test');
const assert = require('node:assert');

const { load, loadCapture } = require('./fakedom.js');

const capture = loadCapture();

function goLive(page) {
    page.socket().onopen();
    page.receive({ type: 'BRIDGE', mode: 'live', serial: true, reset: true });
    capture.forEach((message) => page.receive(message));
    page.flush();
}

test('offline with no data: says so and shows no numbers', () => {
    const page = load();
    const el = page.byId;
    assert.strictEqual(el['status-badge'].textContent, 'OFFLINE');
    assert.match(el['mode-banner'].textContent, /no telemetry bridge at ws:\/\/localhost:9090/);
    assert.strictEqual(el['mode-banner'].hidden, false);
    for (const id of ['metric-uptime', 'metric-ticks', 'metric-irq-rate', 'metric-ctx-switches',
                      'metric-threads', 'metric-link', 'irq-timer', 'irq-drops']) {
        assert.strictEqual(el[id].textContent, '—', id);
    }
    assert.strictEqual(el['sched-gantt'].children[0].textContent, 'No context switches received');
    assert.strictEqual(el['memory-map'].children[0].textContent, 'No memory layout received');
    assert.strictEqual(el['console-text'].textContent, 'No console output received.');
    assert.strictEqual(el['console-input'].disabled, true);
    assert.strictEqual(el['fault-overlay'].style.display, 'none');
    assert.strictEqual(page.interval, undefined, 'demo must not start on its own');
});

test('live: every panel is filled from the recorded kernel session', () => {
    const page = load();
    goLive(page);
    const el = page.byId;
    const lastCounters = capture.filter((m) => m.type === 'COUNTERS').pop();

    assert.strictEqual(el['status-badge'].textContent, 'LIVE');
    assert.strictEqual(el['mode-banner'].hidden, true);
    assert.strictEqual(el['metric-ctx-switches'].textContent, lastCounters.context_switches);
    assert.strictEqual(el['irq-timer'].textContent, lastCounters.timer);
    assert.strictEqual(el['metric-irq-rate'].textContent, '100/s');
    assert.strictEqual(el['metric-link'].textContent, 'clean');
    assert.strictEqual(el['metric-threads'].textContent, 3);

    assert.strictEqual(el['thread-list'].children.length, 3);
    assert.ok(el['sched-gantt'].children.length > 5);
    assert.ok(el['cpu-bars'].children.length >= 2);
    assert.strictEqual(el['memory-map'].children.length, 8);
    assert.ok(el['event-stream'].children.length > 10 && el['event-stream'].children.length <= 80);
    assert.match(el['console-text'].textContent, /Phoenix-8086 boot complete\./);
    assert.match(el['register-caption'].textContent, /^Registers \[\d\] \w+ resumed with at tick \d+/);
    assert.strictEqual(el['console-input'].disabled, false);

    assert.strictEqual(el['fault-overlay'].style.display, 'flex');
    assert.strictEqual(el['fault-reason'].textContent, 'User-triggered panic via shell');
    assert.match(el['fault-thread'].textContent, /^Thread \[1\] shell at tick \d+$/);
    assert.strictEqual(el['fault-registers'].children.length, 11);
});

test('gantt widths are the real durations', () => {
    const page = load();
    goLive(page);
    const blocks = page.byId['sched-gantt'].children;
    const total = blocks.reduce((sum, block) => sum + Number(block.style.flexGrow), 0);
    assert.ok(total > 0 && total <= 300, 'blocks cover at most the 300-tick window');
    blocks.forEach((block) => assert.ok(Number(block.style.flexGrow) >= 1));
});

test('events hide the telemetry thread by default and show it on request', () => {
    const page = load();
    goLive(page);
    const stream = page.byId['event-stream'];
    // The noise is the telemetry thread's own sleeping, waking and switching
    const noise = () => stream.children.filter((e) =>
        /telemetry/.test(e.textContent) && /event-item--(state|switch)/.test(e.className)).length;
    assert.strictEqual(noise(), 0);
    page.byId['event-filter'].listeners.change({ target: { checked: false } });
    page.flush();
    assert.ok(noise() > 0);
});

test('selecting a thread fills the inspector; dismissing hides the fault', () => {
    const page = load();
    goLive(page);
    page.byId['thread-list'].children[1].onclick();
    page.flush();
    assert.strictEqual(page.byId['thread-inspector'].children.length, 7);
    assert.match(page.byId['thread-inspector'].children[6].children[1].textContent,
                 /^\d+ \/ 2048 bytes/);

    page.byId['fault-dismiss'].listeners.click();
    page.flush();
    assert.strictEqual(page.byId['fault-overlay'].style.display, 'none');
});

test('console input is sent to the bridge in live mode only', () => {
    const page = load();
    const form = page.byId['console-form'];
    const input = page.byId['console-input'];

    input.value = 'ps';
    form.listeners.submit({ preventDefault() {} });
    assert.deepStrictEqual(page.sent, [], 'nothing is sent while offline');

    goLive(page);
    input.value = 'ps';
    form.listeners.submit({ preventDefault() {} });
    assert.deepStrictEqual(page.sent, [{ type: 'input', text: 'ps\n' }]);
    assert.strictEqual(input.value, '');
});

test('losing the bridge keeps the data and labels it stale', () => {
    const page = load();
    goLive(page);
    page.socket().onclose();
    page.flush();
    assert.strictEqual(page.byId['status-badge'].textContent, 'OFFLINE');
    assert.match(page.byId['mode-banner'].textContent, /showing the last data received/);
    assert.strictEqual(page.byId['thread-list'].children.length, 3);
    assert.strictEqual(page.byId['console-input'].disabled, true);
});

test('replay mode is labelled and has no input', () => {
    const page = load();
    page.socket().onopen();
    page.receive({ type: 'BRIDGE', mode: 'replay', serial: false, reset: true });
    capture.slice(0, 200).forEach((message) => page.receive(message));
    page.flush();
    assert.strictEqual(page.byId['status-badge'].textContent, 'REPLAY');
    assert.match(page.byId['mode-banner'].textContent, /recorded session/);
    assert.strictEqual(page.byId['console-input'].disabled, true);
});

test('demo mode runs only when asked, is labelled, and yields to real data', () => {
    const page = load();
    page.byId['demo-toggle'].listeners.click();
    page.flush();
    assert.strictEqual(page.byId['status-badge'].textContent, 'DEMO');
    assert.match(page.byId['mode-banner'].textContent, /simulated data/);
    assert.strictEqual(typeof page.interval, 'function');
    for (let i = 0; i < 40; i++) page.interval();
    page.flush();
    assert.strictEqual(page.byId['thread-list'].children.length, 5);

    page.socket().onopen();
    page.flush();
    assert.strictEqual(page.interval, null, 'demo stops when the bridge connects');
    assert.notStrictEqual(page.byId['status-badge'].textContent, 'DEMO');
});

test('every language renders; Arabic switches the page to right-to-left', () => {
    for (const code of ['en', 'de', 'fr', 'es', 'ar']) {
        const page = load({ language: code });
        goLive(page);
        const t = page.i18n.t;
        assert.strictEqual(page.i18n.locale(), code);
        assert.strictEqual(page.documentElement.lang, code);
        assert.strictEqual(page.documentElement.dir, code === 'ar' ? 'rtl' : 'ltr');
        assert.strictEqual(page.byId['status-badge'].textContent, t('mode.live'));
        assert.strictEqual(page.byId['language'].value, code);

        const title = page.elements.find((e) => e.dataset.i18n === 'panel.threads');
        assert.strictEqual(title.textContent, t('panel.threads'));
        assert.strictEqual(page.byId['fault-thread'].textContent.includes('[1] shell'), true);
    }
});

test('the language choice is remembered and can be changed', () => {
    const page = load({ language: 'en', stored: 'fr' });
    assert.strictEqual(page.i18n.locale(), 'fr');
    assert.strictEqual(page.byId['status-badge'].textContent, 'HORS LIGNE');

    page.byId['language'].value = 'de';
    page.byId['language'].listeners.change();
    page.flush();
    assert.strictEqual(page.byId['status-badge'].textContent, 'OFFLINE');
    assert.strictEqual(page.byId['demo-toggle'].textContent, 'Demo starten');

    const unknown = load({ language: 'ja-JP' });
    assert.strictEqual(unknown.i18n.locale(), 'en');
});

test('in-browser emulator mode: kernel bytes are decoded in the page and input goes to its serial port', async () => {
    const fs = require('node:fs');
    const path = require('node:path');
    const raw = fs.readFileSync(path.join(__dirname, 'fixtures', 'boot.bin'));

    let machine = null;
    function FakeV86(options) {
        machine = this;
        this.options = options;
        this.sent = [];
        this.add_listener = (name, handler) => { this.listener = { name, handler }; };
        this.serial0_send = (text) => this.sent.push(text);
    }

    const page = load({ search: '?emulator', V86: FakeV86 });
    assert.strictEqual(page.sockets.length, 0, 'no bridge connection in emulator mode');
    assert.match(page.byId['mode-banner'].textContent, /Starting the in-browser emulator/);

    await new Promise((resolve) => setImmediate(resolve));   // let the start-up promise settle
    page.flush();
    assert.strictEqual(machine.options.fda.url, 'emulator/phoenix8086.img');
    assert.strictEqual(machine.listener.name, 'serial0-output-byte');

    for (const byte of raw) machine.listener.handler(byte);
    page.flush();
    assert.strictEqual(page.byId['status-badge'].textContent, 'IN BROWSER');
    assert.match(page.byId['mode-banner'].textContent, /real kernel running in a PC emulator/);
    assert.strictEqual(page.byId['thread-list'].children.length, 3);
    assert.match(page.byId['console-text'].textContent, /phoenix> /);
    assert.strictEqual(page.byId['demo-toggle'].hidden, true);

    page.byId['console-input'].value = 'ps\u001b';
    page.byId['console-form'].listeners.submit({ preventDefault() {} });
    assert.deepStrictEqual(machine.sent, ['ps\r'], 'input is filtered and sent to the emulated serial port');
});

test('in-browser emulator mode reports a start-up failure instead of showing nothing', async () => {
    function BrokenV86() { throw new Error('WebAssembly is not available'); }
    const page = load({ search: '?emulator', V86: BrokenV86 });
    await new Promise((resolve) => setImmediate(resolve));
    page.flush();
    assert.match(page.byId['mode-banner'].textContent, /could not start: WebAssembly is not available/);
    assert.strictEqual(page.byId['console-input'].disabled, true);
});

test('"?live" overrides a site configured for the emulator', () => {
    const page = load({ search: '?emulator&live' });
    assert.strictEqual(page.sockets.length, 1);
    assert.strictEqual(page.byId['status-badge'].textContent, 'OFFLINE');
});

test('tabs expose their state and move with the arrow keys', () => {
    const page = load();
    const tabs = page.elements.filter((e) => e.classList.contains('view-tab'));
    assert.strictEqual(tabs.length, 4);
    assert.strictEqual(tabs[0].getAttribute('aria-selected'), 'true');

    tabs[0].listeners.keydown({ key: 'ArrowRight', preventDefault() {} });
    assert.strictEqual(tabs[0].getAttribute('aria-selected'), 'false');
    assert.strictEqual(tabs[1].getAttribute('aria-selected'), 'true');
    assert.strictEqual(tabs[1].focused, true);
    assert.ok(page.byId['view-scheduler'].classList.contains('view-content--active'));

    tabs[0].listeners.keydown({ key: 'ArrowLeft', preventDefault() {} });
    assert.strictEqual(tabs[3].getAttribute('aria-selected'), 'true', 'wraps around');
});

test('the panic dialog takes focus and Escape dismisses it', () => {
    const page = load();
    goLive(page);
    assert.strictEqual(page.byId['fault-overlay'].style.display, 'flex');
    assert.strictEqual(page.byId['fault-dismiss'].focused, true);

    page.documentListeners.keydown({ key: 'Escape' });
    page.flush();
    assert.strictEqual(page.byId['fault-overlay'].style.display, 'none');
});

test('thread cards are buttons that say which one is selected', () => {
    const page = load();
    goLive(page);
    const card = () => page.byId['thread-list'].children[1];
    assert.strictEqual(card().getAttribute('role'), 'button');
    assert.strictEqual(card().getAttribute('aria-pressed'), 'false');
    card().onkeydown({ key: ' ', preventDefault() {} });
    page.flush();
    assert.strictEqual(card().getAttribute('aria-pressed'), 'true');
});
