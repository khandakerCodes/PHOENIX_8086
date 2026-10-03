// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Visual Dashboard Application
 *
 * Connects to the telemetry WebSocket bridge and renders
 * real-time kernel state across all dashboard panels.
 *
 * Modules: Boot Timeline, Thread List, Scheduler View,
 * Context Switch Animation, Register Viewer, Memory Map,
 * Thread Inspector, Interrupt Log, Console Mirror.
 */

/* ================================================================
   Configuration
   ================================================================ */
const WS_URL = 'ws://localhost:9090';
const RECONNECT_DELAY = 3000;   // ms before reconnect attempt
const MAX_EVENT_LOG = 80;       // max event stream entries
const TICKS_PER_SECOND = 100;   // Kernel HZ (PIT reprogrammed)

/* ================================================================
   State
   ================================================================ */
const state = {
    connected: false,
    ws: null,

    // Boot
    bootStage: 0,

    // Threads (keyed by TID)
    threads: {},

    // Scheduler
    currentThread: -1,
    ganttHistory: [],     // [{tid, color, timestamp}]
    cpuTime: {},          // tid → ticks

    // Interrupts
    irq: {
        timer: 0,
        keyboard: 0,
        syscall: 0,
        contextSwitches: 0,
    },
    lastIrqTimer: 0,
    irqRate: 0,

    // Ticks
    ticks: 0,
    startTime: Date.now(),

    // Registers
    registers: {
        AX: '0000', BX: '0000', CX: '0000', DX: '0000',
        SP: '7C00', BP: '0000', SI: '0000', DI: '0000',
        CS: '0000', IP: '8000', FLAGS: '0202', SS: '0000',
    },

    // Selected thread for inspector
    selectedThread: null,

    // Console text
    consoleText: '',

    // Context switch
    lastCtxFrom: null,
    lastCtxTo: null,
};

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

const STATE_NAMES = ['READY', 'RUNNING', 'BLOCKED', 'SLEEPING', 'TERMINATED'];

/* ================================================================
   WebSocket Connection
   ================================================================ */
function connectWebSocket() {
    const badge = document.getElementById('status-badge');
    badge.textContent = 'CONNECTING';
    badge.className = 'logo-badge';

    try {
        state.ws = new WebSocket(WS_URL);
    } catch (e) {
        badge.textContent = 'OFFLINE';
        badge.className = 'logo-badge logo-badge--disconnected';
        setTimeout(connectWebSocket, RECONNECT_DELAY);
        return;
    }

    state.ws.onopen = () => {
        state.connected = true;
        badge.textContent = 'LIVE';
        badge.className = 'logo-badge logo-badge--connected';
        addEvent('info', 'Connected to telemetry bridge');
    };

    state.ws.onclose = () => {
        state.connected = false;
        badge.textContent = 'OFFLINE';
        badge.className = 'logo-badge logo-badge--disconnected';
        addEvent('fault', 'Telemetry connection lost');
        setTimeout(connectWebSocket, RECONNECT_DELAY);
    };

    state.ws.onerror = () => {
        state.ws.close();
    };

    state.ws.onmessage = (event) => {
        try {
            const msg = JSON.parse(event.data);
            handleTelemetry(msg);
        } catch (e) {
            console.warn('Failed to parse telemetry:', e);
        }
    };
}

/* ================================================================
   Telemetry Handlers
   ================================================================ */
function handleTelemetry(msg) {
    switch (msg.type) {
        case 'BOOT_STAGE':
            handleBootStage(msg);
            break;
        case 'THREAD_EVENT':
            handleThreadEvent(msg);
            break;
        case 'CONTEXT_SWITCH':
            handleContextSwitch(msg);
            break;
        case 'IRQ_COUNTER':
            handleIrqCounter(msg);
            break;
        case 'REG_SNAPSHOT':
            handleRegSnapshot(msg);
            break;
        case 'MEM_SUMMARY':
            handleMemSummary(msg);
            break;
        case 'FAULT':
            handleFault(msg);
            break;
        case 'CONSOLE':
            handleConsoleLog(msg);
            break;
        default:
            console.log('Unknown telemetry:', msg);
    }
}

function handleConsoleLog(msg) {
    const textEl = document.getElementById('console-text');
    if (textEl) {
        if (textEl.textContent.includes('Waiting for telemetry connection...')) {
            textEl.textContent = '';
        }
        textEl.textContent += (msg.text || '') + '\n';
        const outputContainer = document.getElementById('console-output');
        if (outputContainer) {
            outputContainer.scrollTop = outputContainer.scrollHeight;
        }
    }
}

function handleBootStage(msg) {
    state.bootStage = msg.stage || 0;
    updateBootTimeline();
    addEvent('info', `Boot stage: ${state.bootStage}`);
}

function handleThreadEvent(msg) {
    const tid = msg.tid;
    const eventType = msg.event; // 0=create, 1=destroy, 2=state_change

    if (eventType === 0) {
        // Thread created
        state.threads[tid] = {
            tid: tid,
            name: `Thread ${tid}`,
            state: 0, // READY
            priority: 5,
            cpuTicks: 0,
        };
        addEvent('info', `Thread ${tid} created`);
    } else if (eventType === 1) {
        // Thread destroyed
        if (state.threads[tid]) {
            state.threads[tid].state = 4; // TERMINATED
        }
        addEvent('info', `Thread ${tid} destroyed`);
    }

    updateThreadList();
    updateMetrics();
}

function handleContextSwitch(msg) {
    state.lastCtxFrom = msg.from_tid;
    state.lastCtxTo = msg.to_tid;
    state.currentThread = msg.to_tid;

    // Update thread states
    if (state.threads[msg.from_tid]) {
        state.threads[msg.from_tid].state = 0; // READY
    }
    if (state.threads[msg.to_tid]) {
        state.threads[msg.to_tid].state = 1; // RUNNING
    }

    // Add to Gantt history
    state.ganttHistory.push({
        tid: msg.to_tid,
        color: THREAD_COLORS[msg.to_tid % THREAD_COLORS.length],
        timestamp: Date.now(),
    });
    if (state.ganttHistory.length > 200) {
        state.ganttHistory = state.ganttHistory.slice(-200);
    }

    // Update CPU time
    state.cpuTime[msg.to_tid] = (state.cpuTime[msg.to_tid] || 0) + 1;

    addEvent('switch', `Context switch: ${msg.from_tid} → ${msg.to_tid}`);

    updateThreadList();
    updateContextSwitchView();
    updateSchedulerView();
}

function handleIrqCounter(msg) {
    const prevTimer = state.irq.timer;
    state.irq.timer = msg.timer || 0;
    state.irq.keyboard = msg.keyboard || 0;
    state.irq.syscall = msg.syscall || 0;
    state.irq.contextSwitches = msg.context_switches || 0;

    // Calculate IRQ rate
    state.irqRate = state.irq.timer - prevTimer;
    state.ticks = state.irq.timer;

    updateIrqCounters();
    updateMetrics();
}

function handleRegSnapshot(msg) {
    if (msg.data && msg.data.length >= 24) {
        const d = msg.data;
        const oldRegs = { ...state.registers };

        state.registers.AX = toHex16(d[0] | (d[1] << 8));
        state.registers.BX = toHex16(d[2] | (d[3] << 8));
        state.registers.CX = toHex16(d[4] | (d[5] << 8));
        state.registers.DX = toHex16(d[6] | (d[7] << 8));
        state.registers.SP = toHex16(d[8] | (d[9] << 8));
        state.registers.BP = toHex16(d[10] | (d[11] << 8));
        state.registers.SI = toHex16(d[12] | (d[13] << 8));
        state.registers.DI = toHex16(d[14] | (d[15] << 8));
        state.registers.CS = toHex16(d[16] | (d[17] << 8));
        state.registers.IP = toHex16(d[18] | (d[19] << 8));
        state.registers.FLAGS = toHex16(d[20] | (d[21] << 8));
        state.registers.SS = toHex16(d[22] | (d[23] << 8));

        updateRegisters(oldRegs);
    }
}

function handleMemSummary(msg) {
    addEvent('info', 'Memory summary received');
}

function handleFault(msg) {
    const overlay = document.getElementById('fault-overlay');
    const reason = document.getElementById('fault-reason');

    reason.textContent = msg.data ? `Fault code: ${msg.data.join(', ')}` : 'Unknown fault';
    overlay.style.display = 'flex';

    addEvent('fault', 'KERNEL PANIC!');
}

/* ================================================================
   UI Update Functions
   ================================================================ */

function updateBootTimeline() {
    const steps = document.querySelectorAll('.boot-step');
    steps.forEach((step, index) => {
        step.classList.remove('boot-step--completed', 'boot-step--active');
        if (index < state.bootStage) {
            step.classList.add('boot-step--completed');
        } else if (index === state.bootStage) {
            step.classList.add('boot-step--active');
        }
    });
}

function updateThreadList() {
    const container = document.getElementById('thread-list');
    const tids = Object.keys(state.threads).sort((a, b) => Number(a) - Number(b));

    if (tids.length === 0) return;

    container.innerHTML = '';

    tids.forEach(tidStr => {
        const tid = Number(tidStr);
        const thread = state.threads[tid];
        const stateName = STATE_NAMES[thread.state] || 'UNKNOWN';
        const stateClass = stateName.toLowerCase();
        const isActive = thread.state === 1; // RUNNING

        const card = document.createElement('div');
        card.className = `thread-card${isActive ? ' thread-card--active' : ''}`;
        card.onclick = () => selectThread(tid);

        card.innerHTML = `
            <div class="thread-card__dot thread-card__dot--${stateClass}"></div>
            <div class="thread-card__info">
                <div class="thread-card__name">[${tid}] ${thread.name}</div>
                <div class="thread-card__meta">Pri: ${thread.priority} | CPU: ${thread.cpuTicks || 0}</div>
            </div>
            <span class="thread-card__state thread-card__state--${stateClass}">${stateName}</span>
        `;

        container.appendChild(card);
    });

    // Update scheduler queue
    const queueContainer = document.getElementById('sched-queue');
    const readyThreads = tids.filter(t => state.threads[t].state === 0);

    if (readyThreads.length > 0) {
        queueContainer.innerHTML = readyThreads.map(tid => `
            <div class="sched-queue__item">
                <span style="color: ${THREAD_COLORS[tid % THREAD_COLORS.length]}">●</span>
                [${tid}] ${state.threads[tid].name} (pri: ${state.threads[tid].priority})
            </div>
        `).join('');
    } else {
        queueContainer.innerHTML = '<div class="sched-queue__empty">No threads in ready queue</div>';
    }
}

function updateRegisters(oldRegs) {
    Object.keys(state.registers).forEach(name => {
        const el = document.getElementById(`reg-${name.toLowerCase()}`);
        if (el) {
            const valEl = el.querySelector('.register__val');
            valEl.textContent = state.registers[name];

            if (oldRegs && oldRegs[name] !== state.registers[name]) {
                el.classList.add('register--changed');
                setTimeout(() => el.classList.remove('register--changed'), 1000);
            }
        }
    });
}

function updateIrqCounters() {
    document.getElementById('irq-timer').textContent = state.irq.timer;
    document.getElementById('irq-keyboard').textContent = state.irq.keyboard;
    document.getElementById('irq-syscall').textContent = state.irq.syscall;
    document.getElementById('irq-ctx').textContent = state.irq.contextSwitches;
}

function updateMetrics() {
    const uptime = Math.floor((Date.now() - state.startTime) / 1000);
    const mins = Math.floor(uptime / 60);
    const secs = uptime % 60;

    document.getElementById('metric-uptime').textContent =
        mins > 0 ? `${mins}m ${secs}s` : `${secs}s`;
    document.getElementById('metric-ticks').textContent = state.ticks;
    document.getElementById('metric-irq-rate').textContent = `${state.irqRate}/s`;
    document.getElementById('metric-ctx-switches').textContent = state.irq.contextSwitches;
    document.getElementById('metric-threads').textContent = Object.keys(state.threads).length;
}

function updateSchedulerView() {
    const gantt = document.getElementById('sched-gantt');
    const last50 = state.ganttHistory.slice(-50);

    gantt.innerHTML = last50.map(entry => {
        const height = 20 + Math.random() * 80;
        return `<div class="gantt-block" style="
            background: ${entry.color};
            height: ${height}%;
            flex: 1;
            opacity: 0.8;
        " title="Thread ${entry.tid}"></div>`;
    }).join('');

    // CPU time bars
    const cpuBars = document.getElementById('cpu-bars');
    const totalCpu = Object.values(state.cpuTime).reduce((a, b) => a + b, 1);

    cpuBars.innerHTML = Object.keys(state.cpuTime)
        .sort((a, b) => state.cpuTime[b] - state.cpuTime[a])
        .map(tid => {
            const pct = Math.round((state.cpuTime[tid] / totalCpu) * 100);
            const color = THREAD_COLORS[tid % THREAD_COLORS.length];
            const name = state.threads[tid]?.name || `Thread ${tid}`;
            return `
                <div class="cpu-bar">
                    <span class="cpu-bar__label">${name}</span>
                    <div class="cpu-bar__track">
                        <div class="cpu-bar__fill" style="width: ${pct}%; background: ${color};"></div>
                    </div>
                    <span class="cpu-bar__value">${pct}%</span>
                </div>
            `;
        }).join('');
}

function updateContextSwitchView() {
    document.getElementById('ctx-from-name').textContent =
        state.lastCtxFrom !== null
            ? `[${state.lastCtxFrom}] ${state.threads[state.lastCtxFrom]?.name || '?'}`
            : '—';
    document.getElementById('ctx-to-name').textContent =
        state.lastCtxTo !== null
            ? `[${state.lastCtxTo}] ${state.threads[state.lastCtxTo]?.name || '?'}`
            : '—';

    // Animate phases
    const phases = ['ctx-phase-save', 'ctx-phase-select', 'ctx-phase-restore', 'ctx-phase-iret'];
    phases.forEach((id, i) => {
        const el = document.getElementById(id);
        el.classList.remove('ctx-phase--active');
        setTimeout(() => {
            el.classList.add('ctx-phase--active');
            setTimeout(() => el.classList.remove('ctx-phase--active'), 600);
        }, i * 200);
    });
}

function selectThread(tid) {
    state.selectedThread = tid;
    const thread = state.threads[tid];
    const inspector = document.getElementById('thread-inspector');

    if (!thread) {
        inspector.innerHTML = '<p class="inspector-placeholder">Thread not found</p>';
        return;
    }

    const stateName = STATE_NAMES[thread.state] || 'UNKNOWN';

    inspector.innerHTML = `
        <div class="inspector-field">
            <span class="inspector-field__label">TID</span>
            <span class="inspector-field__value">${tid}</span>
        </div>
        <div class="inspector-field">
            <span class="inspector-field__label">Name</span>
            <span class="inspector-field__value">${thread.name}</span>
        </div>
        <div class="inspector-field">
            <span class="inspector-field__label">State</span>
            <span class="inspector-field__value">${stateName}</span>
        </div>
        <div class="inspector-field">
            <span class="inspector-field__label">Priority</span>
            <span class="inspector-field__value">${thread.priority}</span>
        </div>
        <div class="inspector-field">
            <span class="inspector-field__label">CPU Ticks</span>
            <span class="inspector-field__value">${state.cpuTime[tid] || 0}</span>
        </div>
        <div class="inspector-field">
            <span class="inspector-field__label">Color</span>
            <span class="inspector-field__value" style="color: ${THREAD_COLORS[tid % THREAD_COLORS.length]}">●</span>
        </div>
    `;
}

/* ================================================================
   Event Stream
   ================================================================ */
function addEvent(type, text) {
    const stream = document.getElementById('event-stream');
    const now = new Date();
    const time = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`;

    const item = document.createElement('div');
    item.className = `event-item event-item--${type}`;
    item.textContent = `[${time}] ${text}`;

    stream.insertBefore(item, stream.firstChild);

    // Trim old entries
    while (stream.children.length > MAX_EVENT_LOG) {
        stream.removeChild(stream.lastChild);
    }
}

/* ================================================================
   View Tab Switching
   ================================================================ */
function setupViewTabs() {
    const tabs = document.querySelectorAll('.view-tab');
    const views = document.querySelectorAll('.view-content');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const viewId = `view-${tab.dataset.view}`;

            tabs.forEach(t => t.classList.remove('view-tab--active'));
            views.forEach(v => v.classList.remove('view-content--active'));

            tab.classList.add('view-tab--active');
            document.getElementById(viewId).classList.add('view-content--active');
        });
    });
}

/* ================================================================
   Utilities
   ================================================================ */
function toHex16(value) {
    return (value & 0xFFFF).toString(16).toUpperCase().padStart(4, '0');
}

/* ================================================================
   Demo Mode (simulated data when no telemetry is available)
   ================================================================ */
function startDemoMode() {
    addEvent('info', 'Demo mode: simulating kernel telemetry');

    // Simulate boot sequence
    let bootStep = 0;
    const bootInterval = setInterval(() => {
        if (bootStep >= 9) {
            clearInterval(bootInterval);
            startDemoThreads();
            return;
        }
        state.bootStage = bootStep;
        updateBootTimeline();
        addEvent('info', `Boot stage ${bootStep} completed`);
        bootStep++;
    }, 800);

    // Simulate threads
    function startDemoThreads() {
        const threadNames = ['idle', 'shell', 'demo-A', 'demo-B', 'demo-C'];
        const priorities = [0, 10, 5, 3, 4];

        threadNames.forEach((name, i) => {
            state.threads[i] = {
                tid: i,
                name: name,
                state: i === 0 ? 1 : 0, // idle is RUNNING
                priority: priorities[i],
                cpuTicks: 0,
            };
        });

        state.currentThread = 0;
        updateThreadList();
        updateMetrics();

        // Simulate context switches
        let switchTick = 0;
        setInterval(() => {
            const from = state.currentThread;
            let to;
            do {
                to = Math.floor(Math.random() * threadNames.length);
            } while (to === from);

            // Update states
            state.threads[from].state = 0;
            state.threads[to].state = 1;
            state.currentThread = to;
            state.lastCtxFrom = from;
            state.lastCtxTo = to;
            state.irq.contextSwitches++;

            state.ganttHistory.push({
                tid: to,
                color: THREAD_COLORS[to % THREAD_COLORS.length],
                timestamp: Date.now(),
            });

            state.cpuTime[to] = (state.cpuTime[to] || 0) + 1;

            updateThreadList();
            updateContextSwitchView();
            updateSchedulerView();
            addEvent('switch', `Context switch: ${from} → ${to}`);

            // Update IRQ counters
            switchTick++;
            state.irq.timer = switchTick * TICKS_PER_SECOND;
            state.irq.keyboard = Math.floor(switchTick * 0.3);
            state.ticks = state.irq.timer;
            state.irqRate = TICKS_PER_SECOND;
            updateIrqCounters();
            updateMetrics();

            // Occasionally change registers
            if (switchTick % 3 === 0) {
                const oldRegs = { ...state.registers };
                state.registers.AX = toHex16(Math.floor(Math.random() * 0xFFFF));
                state.registers.BX = toHex16(Math.floor(Math.random() * 0xFFFF));
                state.registers.SP = toHex16(0x7000 + Math.floor(Math.random() * 0xC00));
                state.registers.IP = toHex16(0x8000 + Math.floor(Math.random() * 0x2000));
                updateRegisters(oldRegs);
            }
        }, 1500);
    }
}

/* ================================================================
   Initialization
   ================================================================ */
document.addEventListener('DOMContentLoaded', () => {
    setupViewTabs();
    updateBootTimeline();
    updateMetrics();

    // Try to connect to telemetry
    connectWebSocket();

    // Start demo mode after a brief delay if no connection
    setTimeout(() => {
        if (!state.connected) {
            startDemoMode();
        }
    }, 2000);

    // Update uptime every second
    setInterval(() => {
        updateMetrics();
    }, 1000);
});
