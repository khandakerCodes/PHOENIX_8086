# Phoenix-8086 — Upscaling Plan

| | |
| --- | --- |
| **Document version** | 1.0 |
| **Date** | 2026-10-04 |
| **Code baseline** | Kernel `0.6-dev` (commit `6e061db`) |
| **Relationship to other plans** | [`implementation_plan.md`](implementation_plan.md) gets the project to v1.0. This document is what comes **after** it: the 1.x and 2.0 line. Where the two disagree on anything before v1.0, the implementation plan wins. |

This is a working roadmap for taking Phoenix-8086 from "a small kernel that works and explains itself" to "the most instructive 8086 kernel anyone can run". It is based on a read of the whole tree and a survey of comparable projects (section 2). Every item names what it borrows, why it fits this project, how to build it on a machine with 64 KB segments and no memory protection, and the test that proves it is done.

Checkbox legend, as in the implementation plan: `[ ]` not started · `[/]` in progress · `[x]` done.

---

## 1. Where the Project Stands

Measured on 2026-10-04 from `build/kernel.bin` and the source tree, before any of this plan's work. What has changed since is in the [progress log](#5-progress-log).

| Area | Today | Hard limit or pressure point |
| --- | --- | --- |
| Kernel size | 23.0 KB code, 7.8 KB data, 21.3 KB BSS (image 31 KB) | Code and data are each one 64 KB segment. ~41 KB of code room left; the data segment also holds stacks and the near heap |
| Threads | `MAX_THREADS = 8`, 2 KB kernel stack each, 4 per program | Eight is enough for demos, not for a shell pipeline plus services |
| Scheduler | Priorities + round robin (5-tick slice) + aging (10 ticks), linear scan | One fixed policy; no way to compare policies, which is what OS courses teach |
| Synchronisation | Semaphores, timed wait, mutex (records owner), mailboxes, spin-yield lock | **Mutexes have no priority inheritance**: priority inversion is possible and invisible |
| Memory | First-fit near heap; paragraph-granular far arena with ownership | No fragmentation statistics; no detection when a program writes outside its segments |
| Storage | Native FDC driver + BIOS fallback, FAT12 read-only, root directory only | No writes, no directories, no caching beyond the whole FAT |
| Programs | `PXE2` format, own code and data segments, `open/read/close/exec` | No arguments, exit codes, `wait`, standard input/output, or pipes |
| Faults | Divide/step/breakpoint/overflow traps, kernel-stack red zone, panic screen | Program stack overflow undetected; a crashed service is gone until reboot |
| Telemetry | 13 record types, framed + CRC, 4 KB ring, COM1 at 115,200 baud polled | Timestamps are whole ticks (10 ms); nothing links a cause to its effect |
| Dashboard | Live / Replay / In-browser / Demo / Offline, 5 languages, axe-audited | Replay cannot pause or seek; no export to standard trace tools |
| Testing | 8086 opcode lint, 75 in-kernel assertions, QEMU + DOSBox-X integration, soak, host + browser tests | No host-compiled kernel tests, no fuzzing, no coverage, no cycle-accurate lane |
| Hardware | Emulators only | Never booted on an 8088; polled 115,200 baud on a 4.77 MHz 8088 with an FIFO-less 8250 will drop bytes |

The foundations are unusually solid for a hobby kernel: truthful telemetry, an enforced instruction set, real tests on every push. That is what makes ambitious work safe to attempt. The gaps above are the raw material for this plan.

---

## 2. What Comparable Projects Teach Us

Each project below was studied for one question: *what does it do that would make Phoenix-8086 more instructive, more trustworthy, or more impressive, without breaking its principles?*

| Project | What it is | What we borrow | What we deliberately leave |
| --- | --- | --- | --- |
| [ELKS](https://github.com/ghaerr/elks) | Linux subset for 8086–286; 256 KB minimum; MINIX and FAT file systems; networking; native C toolchain; runs on real XTs and in v86 | Proof that writable file systems, pipes and a real process model fit on an 8086. Its hardware coverage (XT keyboards, CGA/MDA, XTIDE) is the checklist for our real-hardware track | Networking, a native compiler, Unix compatibility. Phoenix's value is being small enough to read whole |
| [Fuzix](https://en.wikipedia.org/wiki/Alan_Cox_(computer_programmer)) (Alan Cox) | Unix-like kernel for very small machines; ~40 KB core; System V features on Z80 and others | How to fit pipes, a VFS and device nodes into a tight budget | System V completeness |
| [xv6](https://pdos.csail.mit.edu/6.1810/2025/labs/cow.html) (MIT 6.1810) | Teaching Unix for RISC-V | Lab structure: each lab is a feature with a ready-made test program and an automatic grader (`make grade`) | Paging-based labs (copy-on-write, lazy allocation) — the 8086 has no page tables |
| [Pintos](https://web.stanford.edu/~ouster/cgi-bin/cs140-spring20/pintos/pintos_2.html) (Stanford CS140) | Teaching OS for x86 | The three classic scheduling projects: an alarm clock with a sorted sleep list, **priority donation** (chains up to depth 8), and a **4.4BSD MLFQS** scheduler chosen by a boot option | Its virtual memory project |
| [MINIX 3](https://en.wikipedia.org/wiki/Minix_3) | Self-healing microkernel OS | The **reincarnation server**: a supervisor that pings services and restarts any that crash or hang | Moving drivers to user space (no MMU to isolate them) |
| [Hubris](https://hubris.oxide.computer/reference/) (Oxide) | Static, message-passing kernel for microcontrollers | A **supervisor task** that owns crash policy outside the kernel; tasks declared at build time; the same code path for "start" and "restart"; [Humility](https://github.com/oxidecomputer/humility) as a debugger that understands the kernel's structures | Memory protection (needs an MPU) |
| [seL4](https://sel4.systems/About/seL4-whitepaper.pdf) | Formally verified capability microkernel | Endpoints and capability-style handles as an IPC design to *teach*; the idea that kernel invariants should be written down and machine-checked | Full proofs. We take the lightweight version: executable models and property tests |
| [Zephyr tracing](https://docs.zephyrproject.org/latest/services/tracing/index.html) / Percepio / SEGGER SystemView | RTOS with ~250 trace hooks and exporters to standard tools | Trace hooks as one macro layer, compiled out to nothing; exporting to a standard format (Zephyr supports CTF) instead of only a custom viewer | Vendor-specific transports |
| [SerenityOS profiler](https://man.serenityos.org/man1/profile.html) | Hobby OS with a built-in sampling profiler | Timer-driven sampling of `CS:IP`, symbolicated against the kernel image, shown as a tree or flame graph; event types for switches, syscalls and allocations | — |
| [Perfetto](https://perfetto.dev/docs/getting-started/other-formats) | Google's trace viewer | The Chrome JSON trace format: slices, counters, instants and **flow arrows**. One converter gives us a professional timeline for free | — |
| [rr](https://rr-project.org/) | Record-and-replay debugger | Deterministic replay and reverse execution, which our in-browser emulator makes possible for the whole machine | — |
| [MartyPC](https://github.com/dbalsom/martypc) and [SingleStepTests/8088](https://github.com/SingleStepTests/8088) | Cycle-accurate PC/XT emulator validated against a real 8088; hardware-generated per-opcode tests | A **cycle-accurate CI lane**, which turns `bench` from "measures the emulator" into real 4.77 MHz numbers | — |
| [MiSTer PCXT](https://github.com/MiSTer-devel/PCXT_MiSTer) / [MCL86](https://hackaday.io/project/188412-mcl86-cycle-accurate-intel-8088-fpga-core) | FPGA PC/XT with a cycle-accurate 8088 core | A reproducible "real hardware" target that contributors can own without hunting for a 1983 machine | — |
| [FreeRTOS mutexes](https://freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/04-Mutexes) | Embedded RTOS | Its simplified inheritance rule: a holder keeps its highest inherited priority until it releases *all* mutexes — simple enough for a 16-bit kernel | — |
| [Project Oberon](https://projectoberon.net/) | Whole system one person can understand | The standard to hold ourselves to: every subsystem still fits in one sitting | — |

**The pattern.** The most respected teaching systems either *teach by building* (xv6, Pintos: labs with graders) or *teach by observing* (SystemView, Perfetto, Serenity's profiler). Nobody does both on a machine small enough to understand completely. That is the niche Phoenix-8086 can own, and it decides what goes into this plan.

---

## 3. Rules for Upscaling

The project's principles ([`projectdetails.md`](projectdetails.md) §2) still apply. These extra rules keep growth from eroding them:

1. **Budget before code.** Every feature declares its cost in code bytes, data bytes and telemetry bandwidth. CI prints a size table on every pull request and fails if a segment passes 90%.
2. **Optional means compiled out.** Anything not needed to boot and run the labs (profiler, gdb stub, extra scheduler policies, write support) sits behind a build flag, as `TELEMETRY=0` does today. The default image stays small enough to read.
3. **Detection, not pretend protection.** The 8086 cannot stop a program writing anywhere. Features in Track B *detect* damage and say so; documentation never calls them protection.
4. **Observable or it did not happen.** A new kernel mechanism ships with its telemetry records and a dashboard view in the same release, or it is not finished.
5. **Every feature is a lab candidate.** If it is a classic OS idea, design it so its core can be removed and handed to a student as an exercise with an automatic grader.
6. **No networking, protected mode or GUI** in 1.x. These are spec non-goals; ELKS covers that ground already.

---

## 4. Tracks

Nine tracks that can progress independently. Effort: **S** about a weekend, **M** about a week, **L** several weeks.

### Track A — Kernel Core: Scheduling and Synchronisation

**Goal:** turn the scheduler from one fixed policy into a small, swappable, measurable subsystem, and close the priority-inversion hole.

- [x] **A1. Priority inheritance for mutexes** (S). *From Pintos and FreeRTOS.* When `mutex_lock` blocks, the owner inherits the waiter's priority, along chains up to depth 4; the loan lives in a TCB field the scheduler keeps when it drops an aging boost, and ends when the owner releases its last mutex (the FreeRTOS rule). New `PRIORITY` telemetry record (`inherit`, `restore`); aging is not reported, it changes too often.
  *Done:* the self-test builds the classic inversion. **Lesson learned while building it:** this kernel's aging already bounds inversion, letting the starved owner back in after about 40–50 ticks, so "high finishes before medium" passes with or without inheritance. The test therefore measures the high thread's *wait*: about 8 ticks with inheritance, about 40 without. It was checked both ways, and against the round-robin lab solution. *Still open:* a priority band on the dashboard timeline.
- [ ] **A2. Sorted sleep queue** (S). *From Pintos's alarm clock.* `check_sleep_queue` scans every thread on every tick. Keep sleepers in a list sorted by wake tick so a tick costs O(1) when nothing is due. Measure the difference with `bench` on the cycle-accurate lane (G5).
- [ ] **A3. Pluggable scheduling policies** (M). *From Pintos's `-mlfqs` switch.* Split `select_next_thread` behind a small operations table (`pick`, `on_tick`, `on_block`, `on_wake`) and ship four policies:
  | Policy | Why it is here |
  | --- | --- |
  | `prio-rr` (today's, default) | Baseline |
  | `rr` | Simplest possible; lab 2 already builds it |
  | `mlfqs` | 4.4BSD feedback queues with `nice` and `recent_cpu`, in fixed-point arithmetic the 8086 can afford |
  | `lottery` | Proportional share; makes CPU shares visible and testable |

  A `sched <policy>` shell command switches at run time; telemetry reports the active policy. Policies other than the default are compile-time optional.
  *Done when:* the same workload runs under each policy and the dashboard's CPU-share chart shows the expected difference, recorded in a capture checked in beside the existing fixtures in `dashboard/test/fixtures/`.
- [ ] **A4. Condition variables and a mutex-guarded bounded buffer** (S). The missing classic primitive; the producer–consumer demo becomes a lab.
- [ ] **A5. Raise `MAX_THREADS` to 16** (M). Requires shrinking per-thread cost: measure actual stack high-water marks (G3) and size stacks per thread instead of a fixed 2 KB. The scheduler's linear scan stays (16 entries is fine) but uses a ready bitmap so idle ticks are cheap.
- [x] **A6. Sub-tick timestamps** (S). Latch PIT channel 0 to read the count within the current tick, giving ~0.84 µs resolution. Unlocks interrupt-latency measurement, the one open `bench` item in Phase 2. *Done:* the PIT moved from mode 3 to mode 2, because in mode 3 the counter runs down twice per tick and a reading is ambiguous. `timer_counts()` checks the PIC for a pending tick and never runs backwards within a tick; that guard was added after QEMU was seen raising IRQ0 late, about once in 500 switches. `CONTEXT_SWITCH` carries the value, and `bench` reports interrupt latency (unrealistic under QEMU, for the same reason).
- [ ] **A7. Kill and exit codes for threads** (S). `thread_kill(tid)` through the existing termination path, plus an exit status the parent can collect (needed by D2).

### Track B — Fault Detection Without an MMU

**Goal:** the 8086 cannot protect memory, but the kernel can notice damage quickly, report it truthfully, and recover the system. This is where Phoenix can show something most teaching kernels hide behind hardware.

- [x] **B1. Program stack overflow detection** (S). The open Phase 4 item. A guard word at the bottom of each program stack and a 64-byte red zone, checked at every switch using the program stack pointer saved at the top of the thread's kernel stack. *Done:* `rogue.bin` overflows its stack and is stopped; a `THREAD_FAULT` record is sent.
- [x] **B2. System-call argument validation** (S). Pointers are checked against the program's data segment bounds, thread entry points against its code size, semaphore/mailbox handles against a registry of objects the kernel handed out, and `free` against the block's owner. Refused calls fail and emit `THREAD_FAULT` (`bad_argument`), not `FAULT`, which the dashboard treats as a panic. *Done:* `rogue.bin` makes nine bad calls and all nine are refused. Building it turned up two real holes, now fixed: a program could free another program's code segment, and any number passed as a handle was used as a kernel pointer. A random-call fuzzer (G4) is still to come.
- [ ] **B3. Kernel integrity watchdog** (M). The idle thread computes a rolling checksum of the kernel code segment and the interrupt vector table, a few hundred bytes per idle pass. A mismatch means something wrote over the kernel: panic with the address range that changed and the last program that ran. Cheap, honest, and a great lecture moment ("this is why the 286 added protected mode").
- [ ] **B4. Supervisor and restartable services** (M). *From the MINIX 3 reincarnation server and the Hubris supervisor.* Declare kernel services (telemetry, a future file-system cache flusher, the shell) in a static table with a restart policy. A supervisor thread receives fault notifications and heartbeats; a service that crashes or misses heartbeats is restarted with the same code path used at boot. The shell surviving its own crash is the demo.
  *Done when:* `crash shell` in a test kills the shell thread with a fault and a new prompt appears within one second, with the restart shown in telemetry.
- [ ] **B5. Fault record with a backtrace** (S). Walk the `BP` chain on the faulting stack (gcc-ia16 keeps frame pointers with the right flags) and send return addresses; the bridge symbolicates them against `kernel.elf` (E3 reuses this).

### Track C — Storage

**Goal:** a writable, structured file system that is still small enough to read, with crash consistency as an explicit teaching topic.

- [ ] **C1. Block cache** (M). A small fixed set of sector buffers (e.g. 8 × 512 B in a far segment) with LRU replacement and hit/miss counters in telemetry. Everything above it reads through it.
- [ ] **C2. Subdirectories and path lookup** (S). FAT12 directories are files; `cd`, `pwd` and paths in `open`.
- [ ] **C3. FAT12 write support** (L). Create, write, truncate, delete, `mkdir`; write-back through the cache with an explicit `sync`. Careful ordering (data, then FAT copies, then directory entry) documented as the crash-consistency lesson.
  *Done when:* CI writes files from the shell and from a program, then runs host `fsck.fat -n` on the image and compares contents with `mtools`; a test cuts power (kills QEMU) mid-write and `fsck.fat` reports only the documented, recoverable kind of damage.
- [ ] **C4. A tiny VFS and device files** (M). *From Fuzix and ELKS.* One `file_ops` table so `/dev/con`, `/dev/com2`, a RAM disk and FAT files share `open/read/write/close`. This is also what makes D3 (pipes) and D4 (redirection) cheap.
- [ ] **C5. RAM disk** (S). A FAT12 volume in the far arena, created at boot; makes write tests fast and safe.
- [x] **C6. Host-side fuzzing of the FAT12 parser** (S). `tests/host/test_fat12.c`: a seeded fuzzer that corrupts the BPB, FAT and root directory of the real image and mounts, walks and reads each copy under ASan and UBSan. *Done:* it found three bugs on its first targeted run, all in `fat_mount` trusting the disk (a wrapped FAT size and a heap overflow, a cluster size of 0 and a division by it, and a FAT buffer reused on remount). Fixed, with one regression case each. 60,000 corrupted images then ran clean. libFuzzer or AFL could still go deeper.

### Track D — The Program Model

**Goal:** programs that behave like small Unix processes, so the shell becomes genuinely useful and the classic process labs become possible.

- [ ] **D1. Program format `PXE3`** (S). Adds requested stack and heap sizes and an ABI version so the loader can reject incompatible programs with a clear message.
- [ ] **D2. Arguments, exit status and `wait`** (S). `run primes.bin 100`; `main(int argc, char **argv)` in the SDK; a `wait` system call; `$?` in the shell.
- [ ] **D3. Pipes** (M). Bounded in-kernel buffers built on the existing semaphores, exposed through the VFS. Telemetry shows bytes in flight, so a learner can *watch* back-pressure block a writer.
- [ ] **D4. Standard input/output and shell redirection** (S). Each program gets handles 0, 1, 2; the shell supports `a | b`, `> file` and `< file`.
- [ ] **D5. A minimal C library for the SDK** (M). `px_printf` (integer and string formats only), `string.h` basics, a far-memory `malloc`. Keeps the "no hidden magic" spirit: one file each, documented.
- [ ] **D6. Out-of-tree SDK** (S). `make sdk-dist` produces a tarball with the header, startup code, linker script and `mkprog.py`, so the Phase 4 exit test is met as written (a program built outside the tree).
- [ ] **D7. Handle-based IPC endpoints** (M, optional). *From seL4, simplified.* Synchronous send/receive/reply on named endpoints with a per-program handle table, as an alternative to global mailbox numbers. A teaching contrast between shared-buffer and rendezvous IPC; kept out of the default build.

### Track E — Observatory 2.0

**Goal:** the dashboard goes from "shows what happened" to "explains why it happened and lets you inspect it with professional tools".

- [ ] **E1. Telemetry protocol v2** (M). Sub-tick timestamps (A6); a `cause` field linking records (the IRQ that woke a thread, the `sem_signal` that unblocked it); new records for priority changes, pipes, block cache and service restarts. v1 captures remain readable; the decoders handle both.
- [x] **E2. Perfetto / Chrome trace export** (S). `python3 -m bridge.trace session.jsonl -o session.json` (`bridge/trace.py`) converts a capture to the Chrome JSON trace format: one track per thread, slices for running time, instants for syscalls and faults, counters for memory and ring fill, **flow arrows** from cause to effect. Anyone can open a Phoenix session in [ui.perfetto.dev](https://ui.perfetto.dev). *Done:* tested against the recorded session in `dashboard/test/fixtures/`. With A6, switch slices have microsecond durations. Flows currently link the two threads at each switch; cause-and-effect flows need protocol v2 (E1).
- [ ] **E3. Sampling profiler** (M). *From SerenityOS.* The timer handler records the interrupted `CS:IP` (and thread) at a configurable rate; the dashboard shows a flame graph per thread, symbolicated against `kernel.elf` and program map files. Compile-time optional.
- [ ] **E4. Causality view** (M). Click a context switch and the dashboard walks back the chain that produced it: *timer IRQ → aging raised B → B preempted A*, or *key press → keyboard IRQ → `sem_signal` → shell woke*. This is the single feature that best delivers the spec's promise ("watch a timer interrupt arrive, see registers saved, see the scheduler pick a thread").
- [ ] **E5. Source linking** (S). Every record type and every causality step links to the exact function in the source (generated from `kernel.elf` line tables at build time). Pairs the moving machine with "the matching 200 lines of source".
- [ ] **E6. Replay controls in the browser** (S). The open Phase 3 item: pause, step, seek and speed in the dashboard, not only on the bridge command line.
- [ ] **E7. Latency and fairness panels** (S). Histograms of scheduling latency (ready → running) and interrupt latency, and a Jain's fairness index over CPU shares; these make A1 and A3 measurable rather than anecdotal.
- [ ] **E8. Bandwidth honesty** (S). The dashboard shows the serial link's utilisation and drop count prominently. On real hardware (H3) the baud rate will drop, and the view must stay truthful about what it lost.

### Track F — Time-Travel Debugging

**Goal:** make the hardest-to-teach bugs (races, priority inversion, lost wakeups) reproducible and rewindable.

- [ ] **F1. Machine snapshots in the browser** (M). v86 can save and restore its full state. The in-browser dashboard takes a snapshot every N seconds plus a log of keyboard input; "rewind 5 s" restores the nearest snapshot and replays input to the chosen point. With a deterministic emulator this is exact, in the spirit of [rr](https://rr-project.org/).
- [ ] **F2. Shareable bug reports** (S). Export a snapshot + input log as one file; a teacher sends a student "the race on line 82" and it reproduces on any machine.
- [ ] **F3. In-kernel GDB stub on COM2** (M). The GDB remote protocol over the second serial port, using the existing single-step and breakpoint vectors. Today `make debug` relies on QEMU's stub; this one works on DOSBox-X, MartyPC, FPGA and real hardware. Compile-time optional.
- [ ] **F4. GDB helpers** (S). A Python script for GDB with `phoenix threads`, `phoenix ready`, `phoenix sem <addr>` commands that decode kernel structures, as Hubris's Humility does.

### Track G — Verification and Quality Engineering

**Goal:** tests that find bugs nobody thought to look for, and numbers that hold up to scrutiny.

- [x] **G1. Host-compiled kernel unit tests** (M). Build `scheduler.c`, `sync.c`, `memory.c`, `fat12.c` for the host against a thin HAL shim; run with sanitizers (ASan, UBSan) under `make test`. Allows coverage (G6) and fuzzing (G4), neither of which is practical inside a 16-bit kernel. The in-kernel `selftest` stays as the on-target check. *Done:* `tests/host/`, `make test-host`, part of `make test`. A simulated machine (`machine.c`) gives kernel threads real stacks through `ucontext`, so `sem_wait` really suspends a thread; `include/types.h` uses `<stdint.h>` on the host, where `long` is 64 bits. The scheduler and thread code are not host-tested yet: they turn 16-bit offsets into pointers.
- [ ] **G2. Executable model of the synchronisation primitives** (M). *A lightweight answer to seL4.* A small model of semaphores, mutexes with inheritance (A1) and the scheduler in Python (or TLA+/PlusCal), explored exhaustively for up to 3 threads and 2 locks, checking: no lost wakeup, no deadlock without a cycle, inheritance bounded. The host tests (G1) replay counterexamples against the real C.
- [x] **G3. Stack high-water marks** (S). Stacks are filled with `A5A5h` at creation (`REP STOSW`); counting untouched words up from the bottom (`REPE SCASW`) gives the deepest use. A downward scan was rejected because an unwritten local buffer would stop it early. *Done:* `stacks` shell command, `THREAD_STATS` fields, the dashboard inspector. First real numbers: the shell peaks at about 360 of 2,048 bytes, so A5's smaller stacks look feasible.
- [/] **G4. Fuzzing** (M). *FAT12 done (C6); program loader, decoders and system calls still to do.* Three targets: the FAT12 parser and the program loader (host, libFuzzer), the telemetry decoders (host, already pure functions), and the system-call interface (a `fuzz.bin` program issuing random calls with random arguments from a seed, run in the soak test; any panic prints the seed).
- [ ] **G5. Cycle-accurate CI lane** (M). Run the image headless on MartyPC as a 4.77 MHz IBM 5150/5160. `bench` numbers from this lane are labelled as real 8088 timings in the README instead of the current warning. Also catches timing-dependent bugs (floppy, keyboard) that QEMU hides. MartyPC is validated against a real 8088 via the SingleStepTests suite, which is what makes the claim defensible.
- [x] **G6. Coverage report** (S). *Done:* `make coverage`, in the CI job summary; 81% of the lines in the four host-tested files. The open Phase 5 item, made possible by G1: line coverage of host-compiled kernel code and the bridge, published by CI.
- [/] **G7. Size and performance regression gate** (S). *Done so far:* `make size` fails past 90% of either segment and CI writes the table to the job summary. *Still open:* comparing against the base branch, and cycle counts (needs G5). CI posts a per-PR table: code/data/BSS bytes, context-switch cycles (from G5), boot time to prompt. A change that grows the kernel by more than 1 KB needs a sentence in the PR explaining why.
- [ ] **G8. Reproducible builds** (S). The open container item from Phase 0, plus a check that two builds produce byte-identical `phoenix8086.img`; the release workflow publishes the hash.

### Track H — Real Hardware

**Goal:** honour the name. Boot on a real 8088/8086 and on an FPGA recreation, with a published compatibility matrix.

- [ ] **H1. XT-class hardware quirks** (M). From the ELKS checklist and the "after v1.0" list in the implementation plan: XT keyboard acknowledge on port 61h, MDA/CGA/EGA detection and a monochrome console at `B000h`, 8250 UART without FIFO, PIT and PIC timing at 4.77 MHz.
- [ ] **H2. 360 KB boot image** (S). A second build target for 5.25" double-density floppies.
- [ ] **H3. Telemetry that survives a slow machine** (M). Interrupt-driven UART transmit instead of polling, a configurable baud rate (9,600–38,400 on a real XT; 115,200 polled on a 4.77 MHz 8088 without a FIFO will drop bytes), and a per-record-type sampling switch so the dashboard can keep up. E8 makes the trade-off visible.
- [ ] **H4. CPU and machine identification** (S). Extend `cpu` to tell the NEC V20/V30 from the 8088 and report the BIOS model byte; reported in `HELLO` telemetry so captures say what they ran on.
- [ ] **H5. FPGA lane** (M). Boot on the MiSTer PCXT core (cycle-accurate MCL86 CPU) and record a capture; documented so any contributor with a MiSTer can reproduce it.
- [ ] **H6. Hardware compatibility matrix** (S). `docs/hardware.md`, filled in by contributors: machine, CPU, video, what works, a capture file as evidence. Truthful by construction.

### Track I — Education and Developer Experience

**Goal:** make Phoenix the easiest kernel to *teach with*, not only to read.

- [ ] **I1. Lab autograder** (M). *From xv6's `make grade`.* Each lab ships tests that run in QEMU; `make grade LAB=2` prints a score. Works in a student's fork through GitHub Actions with no setup.
- [ ] **I2. New labs from this plan** (M, ongoing). Each with a stripped-out starting point, a grader and a worked solution patch that CI keeps applying, like the current three:
  | Lab | Built from | Concept |
  | --- | --- | --- |
  | 04 Alarm clock | A2 | Sorted sleep queues, cost per tick |
  | 05 Priority donation | A1, G2 | Priority inversion and its fix |
  | 06 MLFQS | A3, E7 | Feedback scheduling, fixed-point maths, fairness |
  | 07 Pipes | D3 | Bounded buffers and back-pressure |
  | 08 Write a file | C3 | Crash consistency and ordering |
  | 09 Stack guard | B1, B3 | Detecting what hardware cannot prevent |
- [ ] **I3. Guided tours in the dashboard** (M). Scripted walkthroughs over a recorded capture ("follow one key press from the keyboard to the shell"), highlighting the timeline, registers and source together. Translatable through the existing i18n system.
- [ ] **I4. Architecture Decision Records** (S). `docs/adr/` with one short page per significant choice (true 8086, register frame on the stack, telemetry thread, detection-not-protection). Records the *why* for future contributors; the decisions in the implementation plan's §2 are the first entries.
- [ ] **I5. Documentation site** (S). The open Phase 5 item: generate a site from `docs/` alongside the in-browser demo on GitHub Pages.
- [ ] **I6. Dev container** (S). The open Phase 0 item, now also carrying MartyPC, the fuzzers and the GDB helpers.

---

## 5. Progress Log

| Date | Done | Notes |
| --- | --- | --- |
| 2026-10-04 | A6 | PIT in mode 2; sub-tick switch times in telemetry and the trace export; interrupt latency in `bench`. +650 bytes of kernel code. Needs the 8086 (DOSBox-X) test in CI to confirm mode 2 there |
| 2026-10-04 | G1, G6, C6; G4 started | Host tests: 48 synchronisation, 27 allocator and 2,200+ FAT12 checks under ASan and UBSan, 81% line coverage. The FAT12 fuzzer found three real bugs in mount, all fixed. Cost: +80 bytes of kernel code for the checks |
| 2026-10-04 | A1, B1, B2, E2, G3; G7 started | +2.4 KB code (26,032 bytes, 40% of the segment), +0.6 KB data for all five. 79 self-test assertions (was 75), 91 host tests (was 74), new integration checks for `stacks`, `rogue.bin` and the telemetry records. Lab solution patches 1 and 2 regenerated and re-tested. Not run here: the DOSBox-X 8086 test (not installed on this machine) and the browser tests; CI runs both |

## 6. Release Sequence

Prerequisite: **v1.0 as defined in the implementation plan** (syscall ABI and telemetry protocol v1 frozen, demo hosted, release pipeline run). Nothing here should delay it. Work that changes frozen interfaces (E1, D1) goes into new versions alongside v1, never in place of it.

| Release | Theme | Contents | Exit test |
| --- | --- | --- | --- |
| **1.1** | *Trust the numbers* | G1, G3, G5, G6, G7, G8, A6, B1, B2, E2, E6, I6 | `bench` reports cycle-accurate 8088 timings from CI; any session opens in Perfetto; coverage is published |
| **1.2** | *Scheduling lab bench* | A1, A2, A3, A4, E7, G2, I1, labs 04–06 | One workload, four policies, measurable differences in fairness and latency, each lab auto-graded |
| **1.3** | *Real iron* | H1–H6, E8, F3 | Boots to the shell on at least one real 8088/8086 or the MiSTer PCXT, with a capture in the repo |
| **1.4** | *A useful shell* | C1, C2, C5, C4, D1, D2, D3, D4, D5, D6, lab 07 | `primes.bin 200 \| count.bin > out.txt` works on a RAM disk |
| **1.5** | *Self-healing* | B3, B4, B5, A7, lab 09 | The shell crashes on purpose and comes back; the integrity watchdog catches a deliberate overwrite of the IVT |
| **2.0** | *Explain everything* | E1, E3, E4, E5, F1, F2, F4, C3, C6, G4, A5, I2–I5, lab 08 | A learner can click any context switch, see its cause chain and source, rewind it, and the FAT12 image survives a power cut test |

`2.0` is a major version because telemetry v2 and `PXE3` are new interfaces, even though v1 captures and `PXE2` programs keep working.

```
v1.0 ─► 1.1 Trust the numbers ─┬─► 1.2 Scheduling lab bench ─┐
                               ├─► 1.3 Real iron ────────────┼─► 1.5 Self-healing ─► 2.0 Explain everything
                               └─► 1.4 A useful shell ───────┘
```

1.1 comes first because every later claim (faster sleep queue, fairer scheduler, survivable writes) needs the measurement and testing it provides.

---

## 7. Budgets

Rough, to be replaced by real numbers as each item lands. Default build only; optional features listed separately.

| Item | Code (bytes) | Data + BSS (bytes) | In default build |
| --- | --- | --- | --- |
| Baseline (2026-10-04, before this plan) | 23,568 | 29,846 | — |
| After A1, B1, B2, G3, G7 (measured) | 26,032 | 30,472 | Yes |
| Priority inheritance (A1) | ~400 | ~20 | Yes |
| Sorted sleep queue (A2) | ~200 | ~16 | Yes |
| Policy table + `mlfqs` + `lottery` (A3) | ~1,800 | ~100 | Policies optional |
| Program stack guard + pointer checks (B1, B2) | ~600 | 0 | Yes |
| Supervisor (B4) | ~700 | ~100 | Yes |
| Block cache (C1) | ~600 | buffers in far memory | Yes |
| Directories + write support (C2, C3) | ~4,000 | ~600 | Write optional |
| VFS + pipes + redirection (C4, D3, D4) | ~2,500 | ~400 | Yes |
| Telemetry v2 (E1) | ~800 | 0 (same ring) | Yes |
| Profiler (E3) | ~300 | ~512 | Optional |
| GDB stub (F3) | ~2,500 | ~600 | Optional |
| **Default build after 2.0 (estimate)** | **~35,000** | **~32,000** | Both under 55% of a segment |

Telemetry bandwidth: at 115,200 baud the link carries about 11 KB/s; at 9,600 baud on an XT, under 1 KB/s. E1 and H3 must keep a default session under 1 KB/s with sampling enabled.

---

## 8. Risks

| Risk | Mitigation |
| --- | --- |
| Feature growth makes the kernel unreadable, losing the project's point | Rules 1, 2 and 5; the "fits in one sitting" check is part of review; optional features compile out |
| `gcc-ia16` limits (code size, far-pointer bugs like the `px_getc` one already fixed) | G1 catches logic bugs on the host; G5 and the opcode lint catch target ones; keep the toolchain pinned |
| Real hardware is scarce and varied | FPGA lane (H5) as the reproducible baseline; capture-backed compatibility matrix (H6) rather than claims |
| MartyPC headless automation may need upstream work | Start with its existing debugger/scripting interface; contribute what is missing upstream; DOSBox-X lane stays as the fallback |
| Telemetry v2 splits the ecosystem | Decoders read both versions; v1 captures stay in the test fixtures forever |
| Labs drift from the code they strip out | Solutions are patches CI applies and grades on every push, as today |
| Scope: nine tracks for a small project | Releases are themed and small; each track item is independently useful; nothing in 1.x blocks on 2.0 work |

---

## 9. Explicitly Not Planned

- **Protected mode, paging, 32-bit code.** That would be a different project. The 8086's lack of protection is the subject being taught.
- **Networking.** ELKS already does this well on the same hardware; it would double the kernel's size.
- **DOS compatibility.** A non-goal in the spec.
- **A native compiler or self-hosting.** Interesting, but it moves the learning away from the kernel.
- **Formal proofs of the C code.** G2's executable model gives most of the teaching value at a fraction of the cost.

---

## 10. Sources

Projects and references studied for this plan:

- ELKS — <https://github.com/ghaerr/elks>, <https://en.wikipedia.org/wiki/Embeddable_Linux_Kernel_Subset>
- Fuzix — <https://hackaday.com/2017/04/16/z80-fuzix-is-like-old-fashioned-unix/>, <https://en.wikipedia.org/wiki/Alan_Cox_(computer_programmer)>
- xv6 labs (MIT 6.1810) — <https://pdos.csail.mit.edu/6.1810/2025/labs/cow.html>
- Pintos (Stanford CS140) — <https://web.stanford.edu/~ouster/cgi-bin/cs140-spring20/pintos/pintos_2.html>, <https://web.stanford.edu/class/cs140/projects/pintos/pintos.pdf>
- MINIX 3 — <https://en.wikipedia.org/wiki/Minix_3>, <https://wiki.minix3.org/doku.php?id=developersguide:driverprogramming>
- Hubris and Humility (Oxide) — <https://hubris.oxide.computer/reference/>, <https://github.com/oxidecomputer/humility>, <https://oxide.computer/blog/hubris-and-humility>
- seL4 — <https://sel4.systems/About/seL4-whitepaper.pdf>, <https://sel4.systems/Research/pdfs/sel4-formal-verification-os-kernel.pdf>
- Zephyr tracing — <https://docs.zephyrproject.org/latest/services/tracing/index.html>
- SerenityOS profiler — <https://man.serenityos.org/man1/profile.html>, <https://man.serenityos.org/man1/Applications/Profiler.html>
- Perfetto and the Chrome trace format — <https://perfetto.dev/docs/getting-started/other-formats>
- rr — <https://rr-project.org/>
- MartyPC — <https://github.com/dbalsom/martypc>, <https://martypc.blogspot.com/2023/06/hardware-validating-emulator.html>
- SingleStepTests 8088 — <https://github.com/SingleStepTests/8088>
- MiSTer PCXT and MCL86 — <https://github.com/MiSTer-devel/PCXT_MiSTer>, <https://hackaday.io/project/188412-mcl86-cycle-accurate-intel-8088-fpga-core>
- FreeRTOS mutexes — <https://freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/04-Mutexes>
- Spin model checking of FreeRTOS scheduling — <https://arxiv.org/pdf/2205.07480>
- Project Oberon — <https://projectoberon.net/>
