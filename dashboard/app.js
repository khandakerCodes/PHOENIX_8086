// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Visual Dashboard Application
 *
 * Connects to the telemetry bridge over WebSocket, feeds every message
 * into the model (model.js), and draws the model. Nothing shown on the
 * page comes from anywhere else: before data arrives the panels say so.
 *
 * Modes, always shown in the badge and the banner:
 *   LIVE     — the bridge is relaying a running kernel
 *   REPLAY   — the bridge is playing back a recorded capture
 *   DEMO     — simulated data (demo.js), only when the user starts it
 *   OFFLINE  — no bridge; panels keep whatever was last received
 */
(function () {
    'use strict';

    /* ================================================================
       Configuration
       ================================================================ */
    const WS_URL = 'ws://' + (location.hostname || 'localhost') + ':9090';
    const RECONNECT_DELAY = 3000;       // ms before reconnect attempt
    const RENDER_INTERVAL = 100;        // ms between redraws at most
    const SCHEDULE_WINDOW = 300;        // ticks shown in the scheduler timeline
    const EVENTS_SHOWN = 80;
    const NO_DATA = '—';

    /* Thread colors for visualization */
    const THREAD_COLORS = [
        '#6b7280', // idle (gray)
        '#3b82f6', // blue
        '#10b981', // green
        '#f59e0b', // amber
        '#8b5cf6', // purple
        '#ef4444', // red
        '#06b6d4', // cyan
        '#ec4899', // pink
    ];

    const REGISTER_ORDER = ['ax', 'bx', 'cx', 'dx', 'sp', 'bp', 'si', 'di', 'cs', 'ip', 'flags'];

    /* ================================================================
       State
       ================================================================ */
    const model = PhoenixModel.create();
    let ws = null;
    let wsOpen = false;
    let stopDemo = null;            // non-null while demo mode runs
    let selectedThread = null;
    let hideTelemetryThread = true;
    let faultDismissedTick = null;
    let lastAnimatedSwitch = null;
    let renderPending = false;

    function $(id) {
        return document.getElementById(id);
    }

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) node.textContent = text;
        return node;
    }

    function hex16(value) {
        return (value & 0xFFFF).toString(16).toUpperCase().padStart(4, '0');
    }

    function hex20(value) {
        return value.toString(16).toUpperCase().padStart(5, '0');
    }

    function threadColor(tid) {
        return THREAD_COLORS[tid % THREAD_COLORS.length];
    }

    function threadName(tid) {
        const t = model.threads[tid];
        return '[' + tid + '] ' + (t && t.name ? t.name : '?');
    }

    function seconds(tick) {
        const hz = model.hello ? model.hello.hz : 100;
        return (tick / hz).toFixed(2) + 's';
    }

    function mode() {
        if (stopDemo) return 'demo';
        if (!wsOpen) return 'offline';
        return model.link.mode === 'replay' ? 'replay' : 'live';
    }

    /* ================================================================
       Message intake
       ================================================================ */
    function receive(message) {
        PhoenixModel.apply(model, message);
        if (message.type === 'BRIDGE' && message.reset) {
            selectedThread = null;
            faultDismissedTick = null;
            lastAnimatedSwitch = null;
        }
        scheduleRender();
    }

    function scheduleRender() {
        if (renderPending) return;
        renderPending = true;
        setTimeout(function () {
            renderPending = false;
            render();
        }, RENDER_INTERVAL);
    }

    /* ================================================================
       WebSocket Connection
       ================================================================ */
    function connectWebSocket() {
        try {
            ws = new WebSocket(WS_URL);
        } catch (e) {
            setTimeout(connectWebSocket, RECONNECT_DELAY);
            return;
        }

        ws.onopen = function () {
            wsOpen = true;
            if (stopDemo) {
                stopDemo();         // Real data replaces the demo
                stopDemo = null;
            }
            scheduleRender();
        };

        ws.onclose = function () {
            wsOpen = false;
            model.link.serial = false;
            scheduleRender();
            setTimeout(connectWebSocket, RECONNECT_DELAY);
        };

        ws.onerror = function () {
            ws.close();
        };

        ws.onmessage = function (event) {
            if (stopDemo) return;
            let message;
            try {
                message = JSON.parse(event.data);
            } catch (e) {
                return;
            }
            receive(message);
        };
    }

    function sendInput(text) {
        if (wsOpen && mode() === 'live' && model.link.serial) {
            ws.send(JSON.stringify({ type: 'input', text: text }));
        }
    }

    /* ================================================================
       Rendering
       ================================================================ */
    function render() {
        renderMode();
        renderMetrics();
        renderBootTimeline();
        renderThreadList();
        renderReadyQueue();
        renderScheduler();
        renderContextSwitch();
        renderRegisters();
        renderMemoryMap();
        renderInspector();
        renderCounters();
        renderEvents();
        renderConsole();
        renderFault();
    }

    function renderMode() {
        const badge = $('status-badge');
        const banner = $('mode-banner');
        const current = mode();
        const waiting = current === 'live' && !model.link.serial;

        const labels = { live: 'LIVE', replay: 'REPLAY', demo: 'DEMO', offline: 'OFFLINE' };
        badge.textContent = labels[current];
        badge.className = 'logo-badge logo-badge--' + current;

        let text = '';
        if (current === 'demo') {
            text = 'DEMO MODE — simulated data, not a running kernel';
        } else if (current === 'replay') {
            text = 'REPLAY — recorded session, not live';
        } else if (current === 'offline') {
            text = model.received
                ? 'OFFLINE — bridge disconnected; showing the last data received'
                : 'OFFLINE — no telemetry bridge at ' + WS_URL + '. Nothing to show yet.';
        } else if (waiting) {
            text = 'Bridge connected — waiting for the kernel';
        }
        banner.textContent = text;
        banner.className = 'mode-banner mode-banner--' + (waiting ? 'waiting' : current);
        banner.hidden = text === '';

        $('demo-toggle').hidden = wsOpen;
        $('demo-toggle').textContent = stopDemo ? 'Stop demo' : 'Run demo';

        const live = current === 'live' && model.link.serial;
        $('console-input').disabled = !live;
        $('console-input').placeholder = live ? 'Type a shell command and press Enter'
                                              : 'Input is available in live mode only';
    }

    function renderMetrics() {
        const c = model.counters;
        $('metric-uptime').textContent = model.received ? seconds(model.tick) : NO_DATA;
        $('metric-ticks').textContent = model.received ? model.tick : NO_DATA;
        $('metric-irq-rate').textContent = model.irqRate === null ? NO_DATA : model.irqRate + '/s';
        $('metric-ctx-switches').textContent = c ? c.contextSwitches : NO_DATA;
        $('metric-threads').textContent = model.received ? PhoenixModel.liveThreads(model).length : NO_DATA;

        const link = model.link;
        const problems = link.badFrames + link.lostFrames + (c ? c.drops : 0);
        const linkEl = $('metric-link');
        if (!model.received) {
            linkEl.textContent = NO_DATA;
        } else if (problems === 0) {
            linkEl.textContent = 'clean';
        } else {
            linkEl.textContent = (c ? c.drops : 0) + ' dropped, ' + link.lostFrames + ' lost, ' +
                                 link.badFrames + ' bad';
        }
        linkEl.classList.toggle('metric__value--warn', problems > 0);
    }

    function renderBootTimeline() {
        document.querySelectorAll('.boot-step').forEach(function (step) {
            const stage = Number(step.dataset.stage);
            step.classList.remove('boot-step--completed', 'boot-step--active');
            if (model.bootStage >= stage && model.bootStage > 0) {
                step.classList.add('boot-step--completed');
            } else if (model.bootStage + 1 === stage && model.bootStage > 0) {
                step.classList.add('boot-step--active');
            }
        });
        $('boot-note').textContent = !model.received
            ? 'No boot records received.'
            : model.bootInferred
                ? 'Connected after boot: the stages are inferred from the kernel being up, not observed.'
                : 'Stages before kernel entry cannot report themselves; they are implied by the kernel starting.';
    }

    function renderThreadList() {
        const container = $('thread-list');
        const threads = PhoenixModel.liveThreads(model);
        container.replaceChildren();

        if (threads.length === 0) {
            const card = el('div', 'thread-card thread-card--empty');
            card.appendChild(el('span', 'thread-card__msg', 'No thread data'));
            container.appendChild(card);
            return;
        }

        threads.forEach(function (t) {
            const stateClass = (t.state || 'unknown').toLowerCase();
            const card = el('div', 'thread-card' + (t.state === 'RUNNING' ? ' thread-card--active' : '') +
                                   (t.tid === selectedThread ? ' thread-card--selected' : ''));
            card.tabIndex = 0;
            card.onclick = function () { selectedThread = t.tid; render(); };
            card.onkeydown = function (e) { if (e.key === 'Enter') card.onclick(); };

            const dot = el('div', 'thread-card__dot thread-card__dot--' + stateClass);
            dot.style.background = threadColor(t.tid);
            card.appendChild(dot);

            const info = el('div', 'thread-card__info');
            info.appendChild(el('div', 'thread-card__name', threadName(t.tid)));
            info.appendChild(el('div', 'thread-card__meta',
                'Pri: ' + (t.priority === null ? NO_DATA : t.priority) +
                ' | CPU: ' + (t.cpuTicks === null ? NO_DATA : t.cpuTicks)));
            card.appendChild(info);
            card.appendChild(el('span', 'thread-card__state thread-card__state--' + stateClass,
                                t.state || 'UNKNOWN'));
            container.appendChild(card);
        });
    }

    function renderReadyQueue() {
        const container = $('sched-queue');
        const ready = PhoenixModel.liveThreads(model)
            .filter(function (t) { return t.state === 'READY'; })
            .sort(function (a, b) { return b.priority - a.priority; });
        container.replaceChildren();

        if (ready.length === 0) {
            container.appendChild(el('div', 'sched-queue__empty',
                model.received ? 'No threads ready' : 'No thread data'));
            return;
        }
        ready.forEach(function (t) {
            const item = el('div', 'sched-queue__item');
            const dot = el('span', null, '● ');
            dot.style.color = threadColor(t.tid);
            item.appendChild(dot);
            item.appendChild(document.createTextNode(threadName(t.tid) + ' (pri: ' + t.priority + ')'));
            container.appendChild(item);
        });
    }

    function renderScheduler() {
        const gantt = $('sched-gantt');
        const legend = $('sched-legend');
        const bars = $('cpu-bars');
        const view = PhoenixModel.schedule(model, SCHEDULE_WINDOW);
        gantt.replaceChildren();
        legend.replaceChildren();
        bars.replaceChildren();

        if (view.blocks.length === 0) {
            gantt.appendChild(el('div', 'sched-empty', 'No context switches received'));
            $('sched-window').textContent = '';
            return;
        }

        /* One block per stretch a thread ran; width is its real duration */
        view.blocks.forEach(function (block) {
            const seg = el('div', 'gantt-seg');
            seg.style.flexGrow = String(block.end - block.start);
            seg.style.background = threadColor(block.tid);
            seg.title = threadName(block.tid) + ': ticks ' + block.start + '–' + block.end;
            gantt.appendChild(seg);
        });
        $('sched-window').textContent =
            'Ticks ' + view.blocks[0].start + '–' + view.end + ' (' + seconds(view.end - view.blocks[0].start) + ')';

        view.shares.forEach(function (share) {
            const item = el('span', 'sched-legend__item');
            const dot = el('span', null, '■ ');
            dot.style.color = threadColor(share.tid);
            item.appendChild(dot);
            item.appendChild(document.createTextNode(threadName(share.tid)));
            legend.appendChild(item);

            const bar = el('div', 'cpu-bar');
            bar.appendChild(el('span', 'cpu-bar__label', threadName(share.tid)));
            const track = el('div', 'cpu-bar__track');
            const fill = el('div', 'cpu-bar__fill');
            fill.style.width = share.percent + '%';
            fill.style.background = threadColor(share.tid);
            track.appendChild(fill);
            bar.appendChild(track);
            bar.appendChild(el('span', 'cpu-bar__value', share.percent + '%'));
            bars.appendChild(bar);
        });
    }

    function renderContextSwitch() {
        const sw = model.lastSwitch;
        $('ctx-from-name').textContent = sw ? threadName(sw.from) : NO_DATA;
        $('ctx-to-name').textContent = sw ? threadName(sw.to) : NO_DATA;
        $('ctx-tick').textContent = sw ? 'Last switch at tick ' + sw.tick + ' (' + seconds(sw.tick) + ')'
                                       : 'No context switch received';

        /* Pulse the phases once per new switch, not on every redraw */
        if (sw && sw.tick !== lastAnimatedSwitch) {
            lastAnimatedSwitch = sw.tick;
            ['ctx-phase-save', 'ctx-phase-select', 'ctx-phase-restore', 'ctx-phase-iret']
                .forEach(function (id, i) {
                    const phase = $(id);
                    setTimeout(function () {
                        phase.classList.add('ctx-phase--active');
                        setTimeout(function () { phase.classList.remove('ctx-phase--active'); }, 400);
                    }, i * 120);
                });
        }
    }

    function renderRegisters() {
        const sw = model.lastSwitch;
        REGISTER_ORDER.forEach(function (name) {
            const cell = $('reg-' + name);
            cell.querySelector('.register__val').textContent = sw ? hex16(sw.regs[name]) : '----';
            cell.classList.toggle('register--changed', !!(sw && sw.changed[name]));
        });
        $('reg-ss').querySelector('.register__val').textContent =
            model.hello ? hex16(model.hello.dataSeg) : '----';
        $('register-caption').textContent = sw
            ? 'Registers ' + threadName(sw.to) + ' resumed with at tick ' + sw.tick +
              '. Highlighted: differs from the previous switch.'
            : 'No register data received';
    }

    function renderMemoryMap() {
        const container = $('memory-map');
        const regions = PhoenixModel.memoryRegions(model);
        container.replaceChildren();

        if (regions.length === 0) {
            container.appendChild(el('div', 'sched-queue__empty', 'No memory layout received'));
            return;
        }
        regions.forEach(function (region) {
            const row = el('div', 'mem-region mem-region--' + region.key);
            row.appendChild(el('span', 'mem-region__label', region.label));
            row.appendChild(el('span', 'mem-region__range',
                hex20(region.start) + '–' + hex20(region.end - 1)));
            if (region.total) {
                const percent = Math.round(region.used * 100 / region.total);
                const usage = el('div', 'mem-usage');
                const fill = el('div', 'mem-usage__fill');
                fill.style.width = percent + '%';
                usage.appendChild(fill);
                row.appendChild(usage);
                row.title = region.used + ' of ' + region.total + ' bytes used (' + percent + '%)';
                row.classList.add('mem-region--metered');
            }
            container.appendChild(row);
        });
    }

    function renderInspector() {
        const inspector = $('thread-inspector');
        const t = selectedThread === null ? null : model.threads[selectedThread];
        inspector.replaceChildren();

        if (!t || t.exited) {
            inspector.appendChild(el('p', 'inspector-placeholder',
                t ? 'Thread has exited' : 'Select a thread to inspect'));
            return;
        }

        const used = PhoenixModel.stackUsed(t);
        const fields = [
            ['TID', String(t.tid)],
            ['Name', t.name || NO_DATA],
            ['State', t.state || NO_DATA],
            ['Priority', t.priority === null ? NO_DATA : String(t.priority)],
            ['CPU ticks', t.cpuTicks === null ? NO_DATA : String(t.cpuTicks)],
            ['Stack', t.stackBase === null ? NO_DATA
                : hex16(t.stackBase) + '–' + hex16(t.stackBase + t.stackSize - 1)],
            ['Stack used', used === null ? NO_DATA
                : used + ' / ' + t.stackSize + ' bytes (at last switch-out)'],
        ];
        fields.forEach(function (field) {
            const row = el('div', 'inspector-field');
            row.appendChild(el('span', 'inspector-field__label', field[0]));
            row.appendChild(el('span', 'inspector-field__value', field[1]));
            inspector.appendChild(row);
        });
    }

    function renderCounters() {
        const c = model.counters;
        $('irq-timer').textContent = c ? c.timer : NO_DATA;
        $('irq-keyboard').textContent = c ? c.keyboard : NO_DATA;
        $('irq-syscall').textContent = c ? c.syscall : NO_DATA;
        $('irq-ctx').textContent = c ? c.contextSwitches : NO_DATA;
        $('irq-drops').textContent = c ? c.drops : NO_DATA;
    }

    function isTelemetryNoise(event) {
        if (event.tid === null || (event.kind !== 'state' && event.kind !== 'switch')) {
            return false;
        }
        const t = model.threads[event.tid];
        return !!t && t.name === 'telemetry';
    }

    function renderEvents() {
        const stream = $('event-stream');
        stream.replaceChildren();

        const events = model.events
            .filter(function (event) { return !(hideTelemetryThread && isTelemetryNoise(event)); })
            .slice(0, EVENTS_SHOWN);

        if (events.length === 0) {
            stream.appendChild(el('div', 'event-item event-item--info', 'No events received'));
            return;
        }
        events.forEach(function (event) {
            stream.appendChild(el('div', 'event-item event-item--' + event.kind,
                                  '[' + seconds(event.tick) + '] ' + event.text));
        });
    }

    function renderConsole() {
        const output = $('console-output');
        const text = $('console-text');
        const atBottom = output.scrollHeight - output.scrollTop - output.clientHeight < 40;
        const content = model.received ? model.console.join('\n') : 'No console output received.';
        if (text.textContent !== content) {
            text.textContent = content;
            if (atBottom) {
                output.scrollTop = output.scrollHeight;
            }
        }
    }

    function renderFault() {
        const overlay = $('fault-overlay');
        const fault = model.fault;
        if (!fault || faultDismissedTick === fault.tick) {
            overlay.style.display = 'none';
            return;
        }
        $('fault-reason').textContent = fault.reason;
        $('fault-thread').textContent = 'Thread ' + threadName(fault.tid) + ' at tick ' + fault.tick;
        const regs = $('fault-registers');
        regs.replaceChildren();
        REGISTER_ORDER.forEach(function (name) {
            regs.appendChild(el('span', 'fault-register',
                                name.toUpperCase() + '=' + hex16(fault.regs[name])));
        });
        overlay.style.display = 'flex';
    }

    /* ================================================================
       Controls
       ================================================================ */
    function setupViewTabs() {
        const tabs = document.querySelectorAll('.view-tab');
        const views = document.querySelectorAll('.view-content');

        tabs.forEach(function (tab) {
            tab.addEventListener('click', function () {
                tabs.forEach(function (t) { t.classList.remove('view-tab--active'); });
                views.forEach(function (v) { v.classList.remove('view-content--active'); });
                tab.classList.add('view-tab--active');
                $('view-' + tab.dataset.view).classList.add('view-content--active');
            });
        });
    }

    function setupControls() {
        $('demo-toggle').addEventListener('click', function () {
            if (stopDemo) {
                stopDemo();
                stopDemo = null;
                receive({ type: 'BRIDGE', mode: 'offline', serial: false, reset: true });
            } else if (!wsOpen) {
                stopDemo = PhoenixDemo.start(receive);
            }
            scheduleRender();
        });

        $('console-form').addEventListener('submit', function (event) {
            event.preventDefault();
            const input = $('console-input');
            sendInput(input.value + '\n');
            input.value = '';
        });

        $('event-filter').addEventListener('change', function (event) {
            hideTelemetryThread = event.target.checked;
            scheduleRender();
        });

        $('fault-dismiss').addEventListener('click', function () {
            faultDismissedTick = model.fault ? model.fault.tick : null;
            scheduleRender();
        });
    }

    /* ================================================================
       Initialization
       ================================================================ */
    document.addEventListener('DOMContentLoaded', function () {
        setupViewTabs();
        setupControls();
        render();
        connectWebSocket();

        /* ?demo in the URL asks for the demo explicitly */
        if (new URLSearchParams(location.search).has('demo')) {
            setTimeout(function () {
                if (!wsOpen && !stopDemo) {
                    stopDemo = PhoenixDemo.start(receive);
                }
            }, 500);
        }
    });
}());
