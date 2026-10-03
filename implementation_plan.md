# Phoenix-8086 — Implementation Plan to v1.0

| | |
| --- | --- |
| **Plan version** | 2.0 (supersedes v1, archived at `docs/archive/implementation_plan-v1.md`) |
| **Date** | 2026-10-03 |
| **Specification** | `projectdetails.md` v2.0 |

Checkbox legend: `[ ]` not started · `[/]` in progress · `[x]` done. A phase is finished only when its **exit test** passes in CI.

---

## 1. Verified Baseline (audit of 2026-10-03)

The tree was built with the existing Makefile (gcc 13.3, NASM 2.16) and booted headless in QEMU 8.2 with keystrokes injected. Results:

**What works**

* Two-stage boot reaches `kernel_main`; the boot banner and all `[INIT]` lines print.
* VGA console, serial mirror, IVT installation, timer and keyboard IRQs fire.
* Telemetry packets reach the serial port; the bridge parses four packet types; the dashboard renders.

**What does not work — the kernel is a prototype, not a complete system**

| # | Finding | Evidence | Severity |
| --- | --- | --- | --- |
| B1 | **No thread ever runs.** The scheduler only updates bookkeeping; `context_switch()` is never called. The shell prompt never appears and typing `help` does nothing. | `kernel/scheduler.c:138-176`; QEMU screen dump ends at "boot complete" | Critical |
| B2 | **Telemetry reports context switches that do not happen** (151 switch records in 151 ticks), so the dashboard shows scheduling activity that is not real. | Serial capture | Critical |
| B3 | **Not 8086 code.** Built with `gcc -m16 -march=i386`; the kernel disassembly has ~1,800 lines using 32-bit registers or prefixes, the console uses `FS`, the boot sectors use `pusha`. It needs a 386 or later. | `Makefile:28`, `kernel/console.c:66`, `boot/stage1.asm:90` | Critical (claim) |
| B4 | **Kernel image is truncated on load.** `kernel.bin` is 16,628 bytes; Stage 2 loads a fixed 32 sectors (16,384 bytes). Nothing checks this. | `boot/stage2.asm:23` | Critical |
| B5 | **Thread stacks land on the IVT and boot stack.** `0x10000 + tid*4096` is truncated to 16 bits, so thread 0's guard word is written to address `0000h` (interrupt vector 0) and stacks occupy `0000–7FFFh`. | `kernel/thread.c:38-43,88` | Critical |
| B6 | Stage 2 is padded to `85FFh` and overlaps the kernel load address `8000h`; it survives only because its code is 393 bytes. | `boot/stage2.asm:230` | High |
| B7 | System calls are a stub: the INT 80h handler only increments a counter; `syscall_dispatch` is never called and cannot return values. | `kernel/interrupts.c:108-116` | High |
| B8 | `thread_yield()` executes `int $0x08`, faking a timer tick and sending a spurious EOI. | `kernel/thread.c:154-158` | High |
| B9 | Sleep compares a 32-bit tick counter with a 16-bit wake time; breaks after 65,536 ticks. | `kernel/tcb.h:53`, `kernel/scheduler.c:43` | Medium |
| B10 | Heap is `_kernel_end..FFF0h` (15 KB), while docs and the `memory` command claim `30000–4FFFFh`. | `kernel/memory.c:54-56` | Medium |
| B11 | Semaphore wait queue is unbounded; mutex and mailbox spin instead of blocking; mailbox and keyboard buffers have unguarded races; `broadcast` is missing. | `kernel/sync.c`, `kernel/ipc.c`, `kernel/keyboard.c` | Medium |
| B12 | Panic "register dump" prints the TCB's stale (zero) fields, not live registers. | `kernel/panic.c:57-91` | Medium |
| B13 | Telemetry: busy-wait serial output inside the timer ISR; console text and binary frames share one unescaped stream; 3 of 7 packet types are never emitted; counters truncated to 16 bits. | `kernel/telemetry.c`, `kernel/console.c:136` | Medium |
| B14 | Dashboard silently falls back to random "demo" data, and the scheduler view uses `Math.random()` for bar heights even when live; fonts load from a CDN. | `dashboard/app.js:397,538-640`, `dashboard/index.html:9` | Medium |
| B15 | `phoenix.sh` runs `killall -9 qemu-system-i386` and `fuser -k` on ports, killing unrelated user processes. `tools/run.sh` creates FIFOs it never connects, so its bridge receives nothing. | `phoenix.sh`, `tools/run.sh` | Medium |
| B16 | Dead duplicates (`kernel/*.asm` beside the `.S` files actually built), 17 compiler warnings, stray "Project Visage" `index.html`/`styles.css` and empty `.kombai/` at the root. | tree | Low |
| B17 | No Git repository, licence, README, tests, or CI. | tree | Blocking for open source |

**Status update (2026-10-03, after the toolchain switch):** B3, B4, B5 and B6 are fixed, and B10 is fixed for the near heap (now ~42 KB, reported accurately). From B16, the warnings are gone and the dead assembly is archived in `docs/archive/legacy/`. 

**Status update (Phase 2 core):** B1, B2, B7, B8 and B9 are fixed: threads really run, the shell works, context-switch telemetry is true, INT 80h dispatches and returns values, yield has its own vector, and sleep is 32-bit and wrap-safe. B11 and B12 are fixed as well: `mbox_broadcast` exists, and panic shows the live registers captured at the fault. B10 is fully fixed (far arena above `30000h`, `memory` reports the real layout). Still open: B13 (telemetry design), B14 (dashboard), B15 (launch scripts). `make test` boots the image three times and drives the shell through 26 checks, including a 35-assertion in-kernel self-test.

**Conclusion:** boot, console, and interrupt plumbing are a usable starting point. Threads, scheduling, syscalls, memory layout, and telemetry accuracy must be rebuilt, and that work is the core of this plan.

---

## 2. Decisions Needed From the Maintainer

D1 and D4 were decided on 2026-10-03. D2 and D3 are open; work proceeds on the recommended option unless changed.

| ID | Decision | Recommendation | Why |
| --- | --- | --- | --- |
| D1 | **Decided: true 8086.** Be a true 8086 kernel, or rename to "16-bit real-mode x86 (386+)"? | **True 8086**, using the `ia16-elf-gcc` toolchain (the one ELKS uses). | The 8086 claim is the project's identity. It also gives 16-bit pointers, removing the pointer-size hacks. Cost: a toolchain that must be containerised, and all assembly rewritten once. |
| D2 | Licence | **MIT** | Lowest friction for classrooms and forks. |
| D3 | Public name | Keep **Phoenix-8086** only after a trademark search. | "Phoenix" is also the name of a long-established PC BIOS vendor, which is the same product space. |
| D4 | **Decided: directory renamed to `phoenix-8086`.** Repository name and host | Rename directory/repo from `visage` to `phoenix-8086`; host on GitHub. | Discoverability. |

---

## 3. Phases

### Phase 0 — Foundation (release v0.2)

Goal: a clean, honest, buildable public repository.

- [ ] `git init`; `.gitignore` for `build/`; first commit of the current tree as the v0.1 prototype
- [ ] Add `LICENSE`, SPDX headers, `README.md` (status badge: *pre-alpha*, with the known-issues list from §1)
- [ ] Add `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`, issue/PR templates
- [ ] Remove dead files: `kernel/entry.asm`, `kernel/isr.asm`, `kernel/context_switch.asm`, root `index.html`, root `styles.css`, `.kombai/`, `plan.md`; fold `tools/*.sh` and `phoenix.sh` into Make targets
- [ ] Replace process-killing in the launch script with tracked PIDs only; choose free ports or fail with a clear message
- [/] Makefile: `-Werror`, header dependency tracking (`-MMD`), `make run-headless`, `make test` — `-MMD` and `make test` done
- [x] Build guard: fail if any image exceeds its region (fixes the class of bug B4) — linker `ASSERT`s and `tools/mkimage.py`
- [ ] Container image / devcontainer with pinned NASM, compiler, QEMU, Python
- [ ] CI: build + boot smoke test (headless QEMU, assert "boot complete" on serial)
- [ ] Move docs into `docs/`: architecture, memory map, boot flow; keep `visual.md` as `docs/observatory.md`

**Exit test:** fresh clone → `make test` passes in CI on a clean container.

### Phase 1 — True 8086 (release v0.3)

Goal: an image that contains only 8086 instructions, on the memory layout of spec §4.1.

- [x] Switch `CC` to `ia16-elf-gcc`, flags (`-march=i8086`), and linker script; `tools/get-toolchain.sh` fetches pinned, checksum-verified packages into `.toolchain/`
- [ ] Put the toolchain in the container image (depends on Phase 0)
- [x] Convert `.S` files from `.code16gcc` to plain 16-bit; remove all `pusha/popa`, `FS`, 32-bit registers
- [x] Stage 1 and Stage 2 use 8086-only opcodes (`CPU 8086` directive in NASM enforces this)
- [x] Kernel image header (magic, sizes, checksum) generated at build time; Stage 2 loads by header and verifies (fixes B4)
- [x] New layout: Stage 2 owns `07E00–0FFFF`; kernel code at `1000:0000`, data/stack at `2000:0000` (fixes B5, B6)
- [ ] Boot info block passed to the kernel instead of ad-hoc registers
- [x] Console: far-pointer access to `B800h` without `FS`
- [/] Opcode lint: `make check` (`tools/check8086.py`) disassembles the kernel and fails on non-8086 instructions — done; wiring into CI waits for Phase 0
- [ ] Choose and script the fidelity emulator; add a boot test on it
- [x] Fix all remaining warnings; fixed-width types reviewed for the 16-bit ABI

**Exit test:** opcode lint clean; image boots to the banner on QEMU and on the 8086-only emulator. *(2026-10-03: lint clean and QEMU boot pass via `make test`; the 8086-only emulator run is still outstanding.)*

### Phase 2 — Real Kernel (release v0.4)

Goal: preemptive multitasking that actually runs threads. This is the largest phase.

**2a. Context switch and threads**
- [x] One switch path in assembly (`kernel/isr.S`): push registers → handler saves SP to TCB and returns the next thread's SP → pop → `IRET` (fixes B1). SS never changes because all stacks are in the kernel data segment
- [x] TCB reduced to `SS:SP` + metadata; registers live in the on-stack frame (`frame_t` in `kernel/tcb.h`)
- [x] `thread_create` builds the initial frame; returning from the entry function lands in `thread_exit`
- [x] Stacks allocated inside the kernel data segment with guard words; a dying thread switches away with interrupts off, so its slot is never reused while it is on-CPU
- [x] Dedicated yield vector INT 81h; `int $0x08` removed (fixes B8)
- [x] `kernel_main`'s boot context is adopted as thread 0 and becomes the idle loop

**2b. Scheduler**
- [x] Priority levels with round-robin per level, time quantum (5 ticks), aging (one level per 10 ticks waiting)
- [x] PIT reprogrammed to `HZ` = 100; `HZ` constant used in kernel and dashboard
- [x] 32-bit wrap-safe sleep; `thread_sleep()` (fixes B9)
- [x] Stack-guard failure kills the thread through the normal termination path

**2c. Blocking primitives**
- [x] Blocking semaphore with a bounded wait queue; mutex and mailbox rebuilt on it; killing a blocked thread removes it from the queue (fixes B11). Interrupt-safe critical sections via `hal_irq_save/restore`
- [x] `mbox_broadcast`, non-blocking variants (`sem_trywait`, `mbox_try_send`, `mbox_try_recv`)
- [x] Keyboard buffer made interrupt-safe; `kb_getchar` blocks on a semaphore instead of `HLT`

**2d. System calls**
- [x] INT 80h stub passes the saved frame to C; results written back to the frame's `AX` and carry flag (fixes B7)
- [x] Implement the table in spec §4.8: calls 00h–10h (file calls 18h–1Bh reserved for Phase 4); `docs/syscalls.md`. Blocking calls run on the caller's stack and block in place
- [ ] Calls to destroy semaphores and mailboxes; release a thread's far memory on exit

**2e. Memory**
- [x] Near heap on linker-provided bounds; far-arena allocator above `30000h` sized from INT 12h (fixes B10)
- [x] `memory` command prints the live segment values and allocator bounds

**2f. Faults and shell**
- [x] Exception vectors 0/1/3/4 → panic with the live frame; `kernel_panic()` captures registers through INT 82h; fault record sent to telemetry (fixes B12)
- [x] Stack red zone: a thread is killed when its stack pointer gets within 192 bytes of the stack bottom, before it can reach the neighbouring stack
- [x] Shell runs as a thread; commands added: `ipc`, `syscall`, `nice`, `sleep`, `bench`, `selftest`, `overflow`, `divzero`
- [/] `bench`: context-switch cost and allocator cost done; IRQ latency not measured (needs sub-tick timing from the PIT counter)

**Tests added in this phase**
- [/] Unit tests: done as an in-kernel `selftest` command (heap, far arena, semaphore, mutex, mailbox, sleep, system calls) so they run against the real 16-bit code; host-compiled tests and scheduler-selection tests not yet
- [/] Integration tests (`tools/integration_test.py`, run by `make test`): prompt, `help`, three CPU-bound threads interleave, exit reaping, producer/consumer, syscalls, sleep accuracy, kill, counters, self-test, memory map, `nice`, `sleep`, `bench`, stack-overflow detection, panic and divide-error screens — done; `reboot`, `registers`, `scheduler`, `interrupts`, `clear`, `about` not covered

**Exit test:** integration suite green; `ps` shows ≥4 threads with growing CPU ticks while the shell stays responsive. *(2026-10-03: suite green on QEMU with idle, shell and three demo threads running together.)*

### Phase 3 — Observatory (release v0.5)

Goal: the dashboard shows only the truth, and sessions can be replayed.

- [ ] `docs/telemetry.md`: protocol v1 (framing with escaping, version, sequence, tick, CRC)
- [ ] Kernel: ring buffer filled by events, drained by a low-priority thread; drop counter; compile-time off switch (fixes B13)
- [ ] Console output as a packet type; no raw text on the telemetry port
- [ ] Emit every record type: thread state changes, context switch with register frame, memory summary, syscall, fault, benchmark (fixes B2)
- [ ] Bridge: protocol v1 decoder, capture to file, replay with speed control, input channel to the kernel, `pyproject.toml` with pinned deps, pytest suite
- [ ] Dashboard: split `app.js` into modules; Live / Replay / Demo modes with a permanent on-screen label; remove random data from Live (fixes B14)
- [ ] Dashboard: real memory map, register diff on switch, fault view, console input
- [ ] Vendor fonts and assets; no external requests
- [ ] Dashboard tests driven by recorded captures

**Exit test:** a recorded capture replays to an identical dashboard state; with the bridge disconnected, Live mode shows "no data" rather than simulated values.

### Phase 4 — Programs (release v0.6)

Goal: run separately built programs from disk.

- [ ] Floppy image becomes FAT12 (BPB in Stage 1; kernel and Stage 2 located as files or reserved sectors)
- [ ] Disk read layer and read-only FAT12 driver; `ls`, `cat`, `run`
- [ ] Program format (header + flat binary), loader into the far arena, thread creation with its own data segment
- [ ] File and `exec` syscalls
- [ ] `sdk/`: headers, startup code, linker script, three example programs, build instructions
- [ ] Host unit tests for FAT12 parsing; integration test that runs an SDK program

**Exit test:** a program built outside the kernel tree is copied onto the image with standard host tools and runs from the shell.

### Phase 5 — Hardening and Internationalisation (release v0.9)

- [ ] 1-hour soak test with thread churn in CI (nightly)
- [ ] Coverage report for host unit tests; every subsystem has a design page
- [ ] Pluggable keymaps: US, UK, DE, FR to start; `keymap` command
- [ ] Dashboard locale files, language switcher, right-to-left layout check; accessibility pass
- [ ] `docs/<lang>/` structure and translation guide
- [ ] Build guides verified on Linux, macOS, and Windows (WSL)
- [ ] Documentation site generated from `docs/`
- [ ] Three course labs with solutions on a separate branch
- [ ] Freeze syscall ABI v1 and telemetry protocol v1

**Exit test:** every item in spec §9 except the public demo is met.

### Phase 6 — Public Launch (release v1.0)

- [ ] Browser demo: image booted in an in-browser x86 emulator, wired to the dashboard, hosted on the project site
- [ ] Release pipeline: tagged build publishes image, checksums, capture file, release notes
- [ ] README with screenshots, a short demo recording, and a 15-minute quick start verified by someone new to the project
- [ ] Label 15+ starter issues; publish the 1.x roadmap
- [ ] Announce to OS-development, retro-computing, and education communities

**Exit test:** spec §9 Definition of Done fully met.

### After v1.0

Real-hardware support matrix (XT keyboard acknowledge, CGA/MDA detection, 360 KB image), RAM disk, additional scheduler policies as lab material, more keymaps and translations, FAT12 write support.

---

## 4. Order and Dependencies

```
Phase 0 ─► Phase 1 ─► Phase 2 ─┬─► Phase 3 ─┐
                               └─► Phase 4 ─┴─► Phase 5 ─► Phase 6
```

Phases 3 and 4 can run in parallel once Phase 2 is complete. Phase 1 comes before Phase 2 on purpose: the context switch, ISR stubs, and memory layout are all rewritten for the 16-bit toolchain, so fixing them first under the current compiler would mean writing them twice.

## 5. Risks

| Risk | Mitigation |
| --- | --- |
| `ia16-elf-gcc` is a niche toolchain | Pin it in the container and publish the image; if it proves unworkable, fall back to option B of D1 and rename the target honestly |
| 64 KB code and data segments fill up | Size report in every build; telemetry and debug monitor are compile-time optional |
| Telemetry perturbs timing | Buffered, droppable, measurable via `bench` with telemetry on and off |
| Scope creep before the core works | No Phase 3+ work merges until the Phase 2 exit test is green |
