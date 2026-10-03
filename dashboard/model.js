// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Dashboard Model
 *
 * Pure state: every field here is derived from telemetry messages and
 * nothing else. No DOM, no timers, no random numbers. The same code
 * runs in the browser and under Node for the tests.
 *
 *   const model = PhoenixModel.create();
 *   PhoenixModel.apply(model, message);
 */
(function (root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory();
    } else {
        root.PhoenixModel = factory();
    }
}(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    const MAX_EVENTS = 200;
    const MAX_SEGMENTS = 2000;
    const MAX_CONSOLE_LINES = 300;
    const BOOT_COMPLETE = 7;

    const SYSCALL_NAMES = {
        0x00: 'version', 0x01: 'putc', 0x02: 'puts', 0x03: 'getc',
        0x04: 'thread_create', 0x05: 'thread_exit', 0x06: 'yield', 0x07: 'sleep',
        0x08: 'ticks', 0x09: 'sem_create', 0x0A: 'sem_wait', 0x0B: 'sem_signal',
        0x0C: 'mbox_create', 0x0D: 'mbox_send', 0x0E: 'mbox_recv',
        0x0F: 'alloc', 0x10: 'free',
    };

    function create() {
        return {
            link: { mode: 'offline', serial: false, frames: 0, badFrames: 0, lostFrames: 0 },
            hello: null,            // Machine description, or null until HELLO arrives
            bootStage: 0,
            bootInferred: false,    // true if we joined after boot and never saw the stages
            tick: 0,                // Latest kernel tick seen
            threads: {},            // tid → thread
            current: null,          // tid of the running thread, once a switch has been seen
            segments: [],           // [{tid, start, end}] — who ran, in ticks; end null = still running
            lastSwitch: null,       // {from, to, tick, regs, changed: {reg: bool}}
            counters: null,         // Latest COUNTERS record
            irqRate: null,          // Timer interrupts per second, from two COUNTERS records
            memory: null,           // Latest MEMORY record
            fault: null,            // FAULT record, once the kernel has panicked
            bench: {},              // kind → count per second
            syscalls: 0,            // SYSCALL records seen
            console: [''],          // Lines of console text
            events: [],             // Newest first
            received: 0,            // Kernel messages applied
        };
    }

    function thread(model, tid) {
        if (!model.threads[tid]) {
            model.threads[tid] = {
                tid: tid, name: null, state: null, priority: null,
                cpuTicks: null, sp: null, stackBase: null, stackSize: null,
                exited: false,
            };
        }
        return model.threads[tid];
    }

    /*
     * Events carry a message key and parameters, not text, so the page
     * can show them in any language (see locales/). A parameter named
     * tid, from or to is a thread ID.
     */
    function addEvent(model, kind, tick, key, params, tid) {
        model.events.unshift({ kind: kind, tick: tick, key: key, params: params || {},
                               tid: tid === undefined ? null : tid });
        if (model.events.length > MAX_EVENTS) {
            model.events.length = MAX_EVENTS;
        }
    }

    function appendConsole(model, text) {
        const lines = model.console;
        for (let i = 0; i < text.length; i++) {
            const ch = text[i];
            if (ch === '\n') {
                lines.push('');
            } else if (ch === '\b') {
                lines[lines.length - 1] = lines[lines.length - 1].slice(0, -1);
            } else if (ch !== '\r') {
                lines[lines.length - 1] += ch;
            }
        }
        if (lines.length > MAX_CONSOLE_LINES) {
            lines.splice(0, lines.length - MAX_CONSOLE_LINES);
        }
    }

    /* Thread telemetry arrives only after boot; if we see it, boot finished. */
    function inferBoot(model) {
        if (model.bootStage < BOOT_COMPLETE) {
            model.bootStage = BOOT_COMPLETE;
            model.bootInferred = true;
        }
    }

    const handlers = {
        HELLO: function (model, m) {
            model.hello = {
                version: m.version, hz: m.hz, maxThreads: m.max_threads,
                codeSeg: m.code_seg, dataSeg: m.data_seg,
                farStartSeg: m.far_start_seg, farEndSeg: m.far_end_seg,
                memKb: m.mem_kb, heapStart: m.heap_start, heapEnd: m.heap_end,
            };
            inferBoot(model);
        },

        BOOT_STAGE: function (model, m) {
            model.bootStage = m.stage;
            model.bootInferred = false;
            addEvent(model, 'info', m.tick, 'event.boot', { stage: m.stage });
        },

        THREAD_CREATE: function (model, m) {
            model.threads[m.tid] = {
                tid: m.tid, name: m.name, state: 'READY', priority: m.priority,
                cpuTicks: 0, sp: null, stackBase: null, stackSize: null, exited: false,
            };
            addEvent(model, 'info', m.tick, 'event.created', { tid: m.tid, priority: m.priority }, m.tid);
        },

        THREAD_EXIT: function (model, m) {
            const t = thread(model, m.tid);
            addEvent(model, 'info', m.tick, 'event.exited', { tid: m.tid, name: t.name }, m.tid);
            t.exited = true;
            t.state = 'TERMINATED';
        },

        THREAD_STATE: function (model, m) {
            const t = thread(model, m.tid);
            const priorityChanged = t.priority !== null && t.priority !== m.priority;
            t.state = m.state;
            t.priority = m.priority;
            if (priorityChanged) {
                addEvent(model, 'state', m.tick, 'event.priority', { tid: m.tid, priority: m.priority }, m.tid);
            } else {
                addEvent(model, 'state', m.tick, 'event.state', { tid: m.tid, state: m.state }, m.tid);
            }
        },

        CONTEXT_SWITCH: function (model, m) {
            const from = thread(model, m.from_tid);
            const to = thread(model, m.to_tid);

            /* The outgoing thread is READY unless it already blocked, slept or exited */
            if (from.state === 'RUNNING' || from.state === null) {
                from.state = 'READY';
            }
            to.state = 'RUNNING';
            to.exited = false;
            model.current = m.to_tid;

            const last = model.segments[model.segments.length - 1];
            if (last && last.end === null) {
                last.end = m.tick;
            }
            model.segments.push({ tid: m.to_tid, start: m.tick, end: null });
            if (model.segments.length > MAX_SEGMENTS) {
                model.segments.splice(0, model.segments.length - MAX_SEGMENTS);
            }

            const previous = model.lastSwitch ? model.lastSwitch.regs : null;
            const changed = {};
            Object.keys(m.regs).forEach(function (name) {
                changed[name] = previous !== null && previous[name] !== m.regs[name];
            });
            model.lastSwitch = { from: m.from_tid, to: m.to_tid, tick: m.tick, regs: m.regs, changed: changed };

            addEvent(model, 'switch', m.tick, 'event.switch', { from: m.from_tid, to: m.to_tid }, m.to_tid);
            inferBoot(model);
        },

        COUNTERS: function (model, m) {
            const before = model.counters;
            if (before && m.tick > before.tick && model.hello) {
                model.irqRate = Math.round((m.timer - before.timer) * model.hello.hz / (m.tick - before.tick));
            }
            if (before && m.drops > before.drops) {
                addEvent(model, 'fault', m.tick, 'event.drops', { count: m.drops - before.drops });
            }
            model.counters = {
                tick: m.tick, timer: m.timer, keyboard: m.keyboard, syscall: m.syscall,
                contextSwitches: m.context_switches, drops: m.drops,
            };
        },

        MEMORY: function (model, m) {
            model.memory = {
                heapFree: m.heap_free, heapUsed: m.heap_used,
                farFreeParas: m.far_free_paras, farTotalParas: m.far_total_paras,
            };
        },

        FAULT: function (model, m) {
            model.fault = { tid: m.tid, tick: m.tick, reason: m.reason, regs: m.regs };
            addEvent(model, 'fault', m.tick, 'event.panic', { reason: m.reason }, m.tid);
        },

        CONSOLE: function (model, m) {
            appendConsole(model, m.text);
        },

        SYSCALL: function (model, m) {
            model.syscalls++;
            const name = SYSCALL_NAMES[m.func] || ('0x' + m.func.toString(16));
            addEvent(model, 'syscall', m.tick, 'event.syscall', { tid: m.tid, name: name }, m.tid);
        },

        BENCH: function (model, m) {
            model.bench[m.kind] = m.count;
            addEvent(model, 'info', m.tick, 'event.bench', { kind: m.kind, count: m.count });
        },

        THREAD_STATS: function (model, m) {
            const t = thread(model, m.tid);
            t.name = m.name;
            t.state = m.state;
            t.priority = m.priority;
            t.cpuTicks = m.cpu_ticks;
            t.sp = m.sp;
            t.stackBase = m.stack_base;
            t.stackSize = m.stack_size;
            t.exited = false;
            if (m.state === 'RUNNING' && model.current === null) {
                model.current = m.tid;
            }
            inferBoot(model);
        },
    };

    /** Apply one message from the bridge. Returns the model. */
    function apply(model, message) {
        if (!message || typeof message.type !== 'string') {
            return model;
        }

        if (message.type === 'BRIDGE') {
            if (message.reset) {
                const fresh = create();
                Object.keys(fresh).forEach(function (key) { model[key] = fresh[key]; });
            }
            model.link = {
                mode: message.mode, serial: !!message.serial,
                frames: message.frames || 0,
                badFrames: message.bad_frames || 0,
                lostFrames: message.lost_frames || 0,
            };
            return model;
        }

        const handler = handlers[message.type];
        if (handler) {
            if (typeof message.tick === 'number' && message.tick > model.tick) {
                model.tick = message.tick;
            }
            model.received++;
            handler(model, message);
        }
        return model;
    }

    /* ── Derived views ─────────────────────────── */

    /** Threads that exist now, in TID order. */
    function liveThreads(model) {
        return Object.keys(model.threads)
            .map(function (tid) { return model.threads[tid]; })
            .filter(function (t) { return !t.exited; })
            .sort(function (a, b) { return a.tid - b.tid; });
    }

    /**
     * Who ran during the last `windowTicks` ticks: the segments clipped
     * to the window, and each thread's share of it.
     */
    function schedule(model, windowTicks) {
        const end = model.tick;
        const start = Math.max(0, end - windowTicks);
        const blocks = [];
        const ticksByThread = {};
        let covered = 0;

        model.segments.forEach(function (segment) {
            const from = Math.max(segment.start, start);
            const to = Math.min(segment.end === null ? end : segment.end, end);
            if (to > from) {
                blocks.push({ tid: segment.tid, start: from, end: to });
                ticksByThread[segment.tid] = (ticksByThread[segment.tid] || 0) + (to - from);
                covered += to - from;
            }
        });

        const shares = Object.keys(ticksByThread).map(function (tid) {
            return { tid: Number(tid), ticks: ticksByThread[tid],
                     percent: covered ? Math.round(ticksByThread[tid] * 100 / covered) : 0 };
        }).sort(function (a, b) { return b.ticks - a.ticks; });

        return { start: start, end: end, blocks: blocks, shares: shares, covered: covered };
    }

    /** Bytes of stack in use at the thread's last switch-out, or null if unknown. */
    function stackUsed(thread) {
        if (thread.sp === null || thread.stackBase === null || thread.stackSize === null) {
            return null;
        }
        const used = thread.stackBase + thread.stackSize - thread.sp;
        return used >= 0 && used <= thread.stackSize ? used : null;
    }

    /** Physical memory regions, low to high (named by key). Empty until HELLO arrives. */
    function memoryRegions(model) {
        const h = model.hello;
        if (!h) {
            return [];
        }
        const regions = [
            { key: 'ivt', start: 0x00000, end: 0x00400 },
            { key: 'bios', start: 0x00400, end: 0x00500 },
            { key: 'boot', start: 0x07C00, end: 0x08600 },
            { key: 'kernel', start: h.codeSeg * 16, end: h.codeSeg * 16 + 0x10000 },
            { key: 'threads', start: h.dataSeg * 16, end: h.dataSeg * 16 + h.heapStart },
            { key: 'heap', start: h.dataSeg * 16 + h.heapStart, end: h.dataSeg * 16 + h.heapEnd },
            { key: 'stack', start: h.dataSeg * 16 + h.heapEnd, end: h.dataSeg * 16 + 0x10000 },
        ];
        if (h.farEndSeg > h.farStartSeg) {
            regions.push({ key: 'far', start: h.farStartSeg * 16, end: h.farEndSeg * 16 });
        }
        if (model.memory) {
            const m = model.memory;
            const heap = regions.filter(function (r) { return r.key === 'heap'; })[0];
            heap.used = m.heapUsed;
            heap.total = m.heapUsed + m.heapFree;
            const far = regions.filter(function (r) { return r.key === 'far'; })[0];
            if (far && m.farTotalParas) {
                far.used = (m.farTotalParas - m.farFreeParas) * 16;
                far.total = m.farTotalParas * 16;
            }
        }
        return regions;
    }

    return {
        create: create,
        apply: apply,
        liveThreads: liveThreads,
        schedule: schedule,
        stackUsed: stackUsed,
        memoryRegions: memoryRegions,
        BOOT_COMPLETE: BOOT_COMPLETE,
    };
}));
