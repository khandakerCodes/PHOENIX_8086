#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Dashboard test in a real browser
 *
 * Opens the dashboard in headless Chromium and checks what a stand-in
 * DOM cannot: that the page loads without script errors, fits the
 * window, and behaves when clicked. It covers four situations:
 *
 *   offline      no bridge: the page must say so and show no numbers
 *   replay       a recorded kernel session through the real bridge
 *   Arabic       the right-to-left layout
 *   in browser   the demo site: the kernel running in v86 in the page
 *
 * Screenshots of each are written to the output directory so a person
 * can look at them; the script cannot judge appearance.
 *
 * Usage: node tools/test_dashboard_browser.mjs [output-dir]
 * Needs: npm install playwright-core, a Chromium for it
 *        (npx playwright-core install chromium-headless-shell),
 *        Python with the websockets package, and a built site/
 *        (tools/build_site.sh). Port 9090 must be free.
 */

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';

const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.resolve(process.argv[2] || path.join(project, 'build', 'screenshots'));
const capture = path.join(project, 'dashboard', 'test', 'fixtures', 'session.jsonl');
const BRIDGE_PORT = 9090;

// require.resolve honours NODE_PATH, which a plain import does not
let chromium;
try {
    const require = createRequire(import.meta.url);
    const playwright = await import(pathToFileURL(require.resolve('playwright-core')).href);
    chromium = playwright.chromium || playwright.default.chromium;
} catch (error) {
    console.error('playwright-core is not installed (npm install playwright-core).');
    process.exit(1);
}

const failures = [];
const check = (name, ok, detail = '') => {
    console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${name}`);
    if (!ok) failures.push(`${name}: ${detail}`);
};

function freePort() {
    return new Promise((resolve) => {
        const server = net.createServer();
        server.listen(0, '127.0.0.1', () => {
            const { port } = server.address();
            server.close(() => resolve(port));
        });
    });
}

function portBusy(port) {
    return new Promise((resolve) => {
        const socket = net.connect(port, '127.0.0.1');
        socket.on('connect', () => { socket.destroy(); resolve(true); });
        socket.on('error', () => resolve(false));
    });
}

const children = [];
function start(command, args) {
    const child = spawn(command, args, { cwd: project, stdio: 'ignore' });
    children.push(child);
    return child;
}

async function waitForPort(port, seconds) {
    const deadline = Date.now() + seconds * 1000;
    while (Date.now() < deadline) {
        if (await portBusy(port)) return true;
        await new Promise((resolve) => setTimeout(resolve, 100));
    }
    return false;
}

if (!fs.existsSync(path.join(project, 'site', 'emulator', 'phoenix8086.img'))) {
    console.error('site/ is not built. Run tools/build_site.sh first.');
    process.exit(1);
}
if (await portBusy(BRIDGE_PORT)) {
    console.error(`Port ${BRIDGE_PORT} is in use; stop whatever is using it and try again.`);
    process.exit(1);
}

fs.mkdirSync(out, { recursive: true });
const dashboardPort = await freePort();
const sitePort = await freePort();
start('python3', ['-m', 'http.server', String(dashboardPort), '--bind', '127.0.0.1', '--directory', 'dashboard']);
start('python3', ['-m', 'http.server', String(sitePort), '--bind', '127.0.0.1', '--directory', 'site']);
await waitForPort(dashboardPort, 10);
await waitForPort(sitePort, 10);

const browser = await chromium.launch(process.env.CHROME ? { executablePath: process.env.CHROME } : {});
const errors = [];

async function open(url) {
    const context = await browser.newContext({ viewport: { width: 1440, height: 900 }, locale: 'en-US' });
    const page = await context.newPage();
    page.on('pageerror', (error) => errors.push(`${url}: ${error.message}`));
    page.on('console', (message) => {
        // A refused WebSocket is expected while no bridge is running
        if (message.type() === 'error' && !/WebSocket|ERR_CONNECTION_REFUSED/.test(message.text())) {
            errors.push(`${url}: ${message.text()}`);
        }
    });
    page.on('requestfailed', (request) => {
        if (!request.url().includes(`:${BRIDGE_PORT}`)) errors.push(`${url}: failed to load ${request.url()}`);
    });
    await page.goto(url);
    return page;
}

const fitsWindow = (page) => page.evaluate(() =>
    document.documentElement.scrollWidth <= document.documentElement.clientWidth &&
    document.documentElement.scrollHeight <= document.documentElement.clientHeight);

const bottomBarVisible = (page) => page.evaluate(() => {
    const box = document.getElementById('bottom-bar').getBoundingClientRect();
    return box.top >= 0 && box.bottom <= window.innerHeight + 1;
});

try {
    // ── Offline ─────────────────────────────────
    let page = await open(`http://127.0.0.1:${dashboardPort}/index.html`);
    await page.waitForTimeout(1500);
    check('offline: badge and banner say so',
          await page.textContent('#status-badge') === 'OFFLINE' &&
          /no telemetry bridge/.test(await page.textContent('#mode-banner')));
    check('offline: no numbers are shown', await page.textContent('#metric-ticks') === '—');
    check('offline: page fits the window', await fitsWindow(page));
    await page.screenshot({ path: path.join(out, 'offline.png') });
    await page.close();

    // ── Replay through the real bridge ──────────
    start('python3', ['bridge/serial_ws_bridge.py', '--replay', capture, '--speed', '8',
                      '--ws-port', String(BRIDGE_PORT)]);
    check('bridge started', await waitForPort(BRIDGE_PORT, 15));

    page = await open(`http://127.0.0.1:${dashboardPort}/index.html`);
    await page.waitForSelector('#fault-overlay', { state: 'visible', timeout: 30000 });
    check('replay: the recorded panic is shown',
          await page.textContent('#fault-reason') === 'User-triggered panic via shell');
    await page.screenshot({ path: path.join(out, 'replay-panic.png') });
    await page.click('#fault-dismiss');
    await page.waitForTimeout(400);     // the page redraws at most every 100 ms
    check('replay: the panic can be dismissed', !(await page.isVisible('#fault-overlay')));

    check('replay: badge says REPLAY', await page.textContent('#status-badge') === 'REPLAY');
    check('replay: threads, memory map and timeline are drawn',
          await page.locator('#thread-list .thread-card').count() === 3 &&
          await page.locator('#memory-map .mem-region').count() === 8);

    for (const view of ['boot', 'scheduler', 'context-switch', 'console']) {
        await page.click(`[data-view="${view}"]`);
        await page.waitForTimeout(300);
        check(`replay: ${view} view fits the window with the bottom bar visible`,
              await fitsWindow(page) && await bottomBarVisible(page));
        await page.screenshot({ path: path.join(out, `replay-${view}.png`) });
    }
    check('replay: scheduler timeline has blocks',
          await page.locator('#sched-gantt .gantt-seg').count() > 5);

    const phases = await page.evaluate(() => {
        document.querySelector('[data-view="context-switch"]').click();
        return [...document.querySelectorAll('.ctx-phase')].map((e) => Math.round(e.getBoundingClientRect().top));
    });
    check('replay: the four context-switch steps sit on one line',
          phases.length === 4 && Math.max(...phases) - Math.min(...phases) < 20, String(phases));

    await page.click('#thread-list .thread-card:nth-child(2)');
    check('replay: clicking a thread fills the inspector',
          await page.locator('#thread-inspector .inspector-field').count() === 7);
    check('replay: console input is disabled', await page.isDisabled('#console-input'));

    // ── Arabic, right to left ───────────────────
    await page.click('[data-view="scheduler"]');
    await page.selectOption('#language', 'ar');
    await page.waitForTimeout(300);
    const rtl = await page.evaluate(() => {
        const left = document.getElementById('panel-left').getBoundingClientRect();
        const right = document.getElementById('panel-right').getBoundingClientRect();
        return { dir: document.documentElement.dir, mirrored: left.left > right.left,
                 registers: getComputedStyle(document.getElementById('register-grid')).direction };
    });
    check('Arabic: the page is right-to-left and the side panels swap', rtl.dir === 'rtl' && rtl.mirrored,
          JSON.stringify(rtl));
    check('Arabic: register values stay left-to-right', rtl.registers === 'ltr');
    check('Arabic: page fits the window', await fitsWindow(page));
    await page.screenshot({ path: path.join(out, 'arabic.png') });
    await page.close();

    // ── The demo site: kernel running in the page ──
    page = await open(`http://127.0.0.1:${sitePort}/index.html`);
    await page.waitForFunction(() => /phoenix> /.test(document.getElementById('console-text').textContent),
                               null, { timeout: 90000 });
    check('in browser: the kernel boots in the page', true);
    check('in browser: badge says IN BROWSER', await page.textContent('#status-badge') === 'IN BROWSER');
    await page.click('[data-view="console"]');
    await page.fill('#console-input', 'run primes.bin');
    await page.press('#console-input', 'Enter');
    await page.waitForFunction(() => /largest 997/.test(document.getElementById('console-text').textContent),
                               null, { timeout: 60000 });
    check('in browser: a typed command loads and runs a program', true);
    check('in browser: telemetry link is clean', await page.textContent('#metric-link') === 'clean');
    const input = await page.evaluate(() => {
        const box = document.getElementById('console-input').getBoundingClientRect();
        return box.bottom <= document.getElementById('bottom-bar').getBoundingClientRect().top + 1;
    });
    check('in browser: the console input is not hidden behind the bottom bar', input);
    await page.waitForTimeout(500);
    await page.screenshot({ path: path.join(out, 'in-browser.png') });
    await page.close();
} catch (error) {
    check('script completed', false, error.message.split('\n')[0]);
} finally {
    await browser.close();
    children.forEach((child) => child.kill());
}

check('no script errors or failed requests', errors.length === 0, errors.slice(0, 5).join(' | '));

if (failures.length) {
    console.log(`Dashboard browser test FAILED (${failures.length}):`);
    failures.forEach((failure) => console.log(`  - ${failure.slice(0, 400)}`));
    process.exit(1);
}
console.log(`  Dashboard browser test passed; screenshots in ${out}`);
