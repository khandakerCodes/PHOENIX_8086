// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Demo Mode
 *
 * SIMULATED DATA. Produces a fixed, scripted sequence of telemetry
 * messages so the dashboard can be shown without a kernel. It runs
 * only when the user asks for it, never as a fallback, and the page
 * labels everything it shows as simulated.
 */
(function (root) {
    'use strict';

    const HZ = 100;
    const THREADS = [
        { tid: 0, name: 'idle', priority: 0 },
        { tid: 1, name: 'shell', priority: 10 },
        { tid: 2, name: 'demo-A', priority: 5 },
        { tid: 3, name: 'demo-B', priority: 4 },
        { tid: 4, name: 'demo-C', priority: 3 },
    ];
    /* Which thread runs in each 50 ms step; repeats */
    const ROTATION = [2, 3, 2, 4, 2, 3, 1, 0, 2, 3, 4, 2];

    function start(deliver) {
        let tick = 0;
        let step = 0;
        let current = 0;
        const cpu = THREADS.map(function () { return 0; });

        function send(message) {
            message.tick = tick;
            deliver(message);
        }

        deliver({ type: 'BRIDGE', mode: 'demo', serial: false, reset: true });
        for (let stage = 1; stage <= 7; stage++) {
            send({ type: 'BOOT_STAGE', stage: stage });
        }
        send({ type: 'CONSOLE', text: 'Phoenix-8086 demo mode (simulated data)\nphoenix> ' });
        THREADS.forEach(function (t) {
            send({ type: 'THREAD_CREATE', tid: t.tid, priority: t.priority, name: t.name });
        });

        const timer = setInterval(function () {
            tick += 5;
            step++;
            cpu[current] += 5;

            const next = ROTATION[step % ROTATION.length];
            if (next !== current) {
                const base = 0x2000 + next * 0x800;
                send({
                    type: 'CONTEXT_SWITCH', from_tid: current, to_tid: next,
                    regs: {
                        ip: 0x0400 + next * 0x120 + (step % 7) * 4, cs: 0x1000, flags: 0x0202,
                        sp: base + 0x7C0 - (step % 5) * 6, ax: step & 0xFFFF, bx: next * 0x111,
                        cx: (step * 3) & 0xFF, dx: 0, si: base, di: base + 0x40, bp: base + 0x7D0,
                    },
                });
                current = next;
            }

            if (step % 4 === 0) {
                send({ type: 'COUNTERS', timer: tick, keyboard: Math.floor(step / 40),
                       syscall: 0, context_switches: step, drops: 0 });
            }
            if (step % 20 === 0) {
                send({ type: 'HELLO', version: 1, hz: HZ, max_threads: 8, code_seg: 0x1000,
                       data_seg: 0x2000, far_start_seg: 0x3000, far_end_seg: 0x9FC0, mem_kb: 639,
                       heap_start: 0x6800, heap_end: 0xF800 });
                send({ type: 'MEMORY', heap_free: 36000, heap_used: 824,
                       far_free_paras: 28000, far_total_paras: 28608 });
                THREADS.forEach(function (t, i) {
                    send({ type: 'THREAD_STATS', tid: t.tid,
                           state: t.tid === current ? 'RUNNING' : 'READY',
                           priority: t.priority, cpu_ticks: cpu[i],
                           sp: 0x2000 + t.tid * 0x800 + 0x700, stack_base: 0x2000 + t.tid * 0x800,
                           stack_size: 2048, name: t.name });
                });
            }
        }, 50);

        return function stop() { clearInterval(timer); };
    }

    root.PhoenixDemo = { start: start };
}(typeof self !== 'undefined' ? self : this));
