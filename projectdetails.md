# Phoenix-8086 — Master Project Specification

| | |
| --- | --- |
| **Spec version** | 2.0 (supersedes v1, archived at `docs/archive/projectdetails-v1.md`) |
| **Date** | 2026-10-03 |
| **Code baseline** | Kernel v0.1 (prototype) |
| **Target release** | v1.0 — first public, stable release |
| **Roadmap** | `implementation_plan.md` |

This document is the source of truth for what Phoenix-8086 is, what it must do, and what "done" means. Where the code and this document disagree, the code is wrong or this document must be changed in the same pull request.

---

## 1. What Phoenix-8086 Is

**Phoenix-8086 is an observable teaching kernel: a small preemptive operating system for the Intel 8086 that explains itself while it runs.**

It has two halves that ship together:

1. **The kernel** — a bare-metal, real-mode, preemptive multitasking kernel that boots from a floppy image without DOS, written in C and assembly, and restricted to the 8086 instruction set.
2. **The observatory** — a telemetry stream, a host-side bridge, and a web dashboard that show boot stages, thread states, context switches, interrupts, and memory live, driven only by what the kernel reports.

### Why it should exist

Teaching kernels (xv6, MikeOS, ELKS and others) show the *code*. Phoenix-8086 shows the *machine in motion*: a learner can watch a timer interrupt arrive, see registers saved, see the scheduler pick a thread, and read the matching 200 lines of source. That combination is the project's reason to exist and the bar every feature is measured against.

### Audience

* **Students and self-learners** studying operating systems or computer architecture.
* **Educators** who need a small, readable kernel with ready-made labs and a visual aid for lectures.
* **Retro-computing and systems hobbyists** who want an 8086 kernel that runs on period-accurate emulators and real hardware.
* **Contributors** looking for a kernel small enough to understand completely.

### Non-goals

* Not a DOS clone or DOS-compatible environment.
* No protected mode, paging, or 32-bit support in the 1.x line.
* Not a production or security-hardened OS. Real mode has no memory protection, and the documentation says so plainly.
* No networking, no GUI inside the kernel.

---

## 2. Principles

1. **Truthful.** Every claim in the docs, the shell, and the dashboard is backed by running code. The dashboard never displays invented data as if it were live.
2. **8086-faithful.** The shipped image contains only 8086/8088 instructions. This is enforced by tooling, not by intent.
3. **Readable first.** A subsystem should fit in one file a learner can read in one sitting. Clever code needs a comment explaining why.
4. **Verified.** Every subsystem has an automated test. A feature without a test is not complete.
5. **Reproducible.** One command builds the image on Linux, macOS, and Windows (WSL or container) with pinned tool versions.
6. **International by default.** English is the source language, but docs, dashboard strings, and keyboard layouts are structured for translation from the start. Nothing requires network access at runtime.

---

## 3. Target Platform

| Item | Requirement |
| --- | --- |
| CPU | Intel 8086/8088 instruction set only. No 186+ opcodes (`pusha`, `push imm`, shifts by immediate > 1), no 386 registers or prefixes. |
| Mode | 16-bit real mode, segmented addressing |
| Memory | 256 KB minimum, 640 KB conventional maximum |
| Boot medium | 1.44 MB floppy image (FAT12 from v0.6); 360 KB image as a stretch target for XT-class machines |
| Firmware | IBM PC-compatible BIOS (INT 10h, 12h, 13h during boot only) |
| Devices | PIC 8259, PIT 8253/8254, keyboard controller (port 60h), text video (colour B800h), UART 8250/16550 on COM1 |
| Reference emulator | QEMU `qemu-system-i386` for development and CI integration tests |
| Fidelity emulator | DOSBox-X with its CPU set to 8086 (`make test-8086`), to prove the image runs without 186+/386 features; a cycle-accurate 8088 emulator (MartyPC, 86Box) as a later addition |
| Real hardware | Tested on at least one physical or FPGA 8088/8086 machine before v1.0 (stretch: required for 1.1) |

**Toolchain:** NASM for the boot sectors; a true 16-bit C compiler (`ia16-elf-gcc`, see decision D1 in the roadmap) for the kernel; GNU Make; Python 3 for the bridge and test harness. All versions are pinned in a container image.

---

## 4. System Architecture

```
                 BIOS
                  │
        Stage 1 boot sector (512 B)
                  │
        Stage 2 loader ── reads kernel image header, loads, verifies checksum
                  │
   ┌──────────────┴───────────────────────────────────────────┐
   │                        KERNEL                            │
   │  HAL: port I/O, PIC, PIT, UART, video, keyboard          │
   │  Interrupt layer: IVT ownership, ISR stubs, dispatch     │
   │  Scheduler + threads ── context switch (assembly)        │
   │  Memory: static map, near heap, far arena                │
   │  Sync + IPC: semaphores, mutexes, mailboxes              │
   │  System calls: INT 80h                                   │
   │  Services: console, shell, debug monitor, panic          │
   │  Telemetry: ring buffer → UART                           │
   └──────────────┬───────────────────────────────────────────┘
                  │ serial (telemetry protocol v1)
            Bridge (Python) ── WebSocket ── Dashboard (browser)
```

### 4.1 Physical memory map

| Range | Use |
| --- | --- |
| `00000–003FF` | Interrupt Vector Table |
| `00400–004FF` | BIOS Data Area |
| `00500–07BFF` | Boot-time stack; free after kernel start |
| `07C00–07DFF` | Stage 1 |
| `07E00–0FFFF` | Stage 2 (code + buffers) |
| `10000–1FFFF` | Kernel code segment (CS = `1000h`) |
| `20000–2FFFF` | Kernel data segment (DS = SS = `2000h`): data, BSS, TCB table, thread stacks, near heap |
| `30000–top` | Far arena: loaded programs and large allocations, up to the limit reported by INT 12h |

No region may overlap another. The build fails if any image exceeds its region.

### 4.2 Boot

* **Stage 1** fits in one sector, uses only 8086 instructions, preserves the boot drive, loads Stage 2 with retries, and reports errors with a code.
* **Stage 2** detects conventional memory, reads the **kernel image header** (magic, version, code size, data size, BSS size, entry point, checksum), loads exactly that many sectors, verifies the checksum, and passes a **boot info block** (drive, memory size, kernel sizes) to the kernel.
* The kernel never depends on a hard-coded sector count.

### 4.3 Threads and context switching

* A thread's saved registers live **on its own stack** as an interrupt frame. The TCB stores only the saved `SS:SP` plus metadata.
* TCB fields the assembly depends on are fixed: `+00h SP`, `+02h SS`. Everything else (state, base priority, effective priority, TID, stack base and size, 32-bit wake tick, 32-bit CPU ticks, wait object, name) is C-only and may evolve.
* One switch path is shared by timer preemption and voluntary yield: push all registers → store `SS:SP` in the current TCB → run the scheduler → load `SS:SP` of the chosen TCB → pop registers → `IRET`.
* A new thread starts from a fabricated initial frame. Returning from the entry function ends the thread cleanly.
* Lifecycle: `create`, `exit`, `kill`, `suspend`, `resume`, `yield`, `sleep`. States: `READY`, `RUNNING`, `BLOCKED`, `SLEEPING`, `ZOMBIE`.
* Each stack has a guard word checked on every switch; a violated guard kills the thread and raises a telemetry fault.
* Thread 0 is the idle thread and executes `HLT`.

### 4.4 Scheduler

* Timer-driven preemption at a configurable rate (default 100 Hz, PIT reprogrammed by the kernel).
* Fixed priority levels with round-robin inside each level and a per-thread time quantum.
* Aging raises the effective priority of starved threads; the base priority is restored when they run.
* Blocking primitives remove a thread from the run set; they never busy-wait.
* Sleep uses a 32-bit wake tick and is wrap-safe.

### 4.5 Interrupts

* The kernel owns the IVT entries it uses: IRQ0 (timer), IRQ1 (keyboard), INT 80h (system calls), plus CPU exceptions 0 (divide error), 1, 3, 4, which route to the panic/debug path.
* Each IRQ is acknowledged exactly once. Handlers do minimal work and defer the rest.
* Voluntary yield does not reuse the timer vector.

### 4.6 Memory management

* Static regions as in §4.1, enforced by the linker script and build checks.
* **Near heap** (inside the kernel data segment): first-fit allocator with coalescing, used for kernel objects.
* **Far arena** (above `30000h`): paragraph-granular allocator for program images and large buffers.
* The `memory` shell command and the dashboard memory map report the real layout from the same constants.

### 4.7 Synchronisation and IPC

* Semaphore, mutex (owner-tracked, with a blocking wait queue), and interrupt-masking critical sections.
* Mailbox: bounded queue of 16-bit messages with blocking `send` / `recv`, non-blocking variants, and `broadcast`.
* All wait queues are bounded and checked.

### 4.8 System calls

`INT 80h`, function number in `AH`, arguments in `AL/BX/CX/DX`, result in `AX`, carry flag set on error. The table is versioned and documented in `docs/syscalls.md`.

| AH | Call | Purpose |
| --- | --- | --- |
| 00h | `version` | ABI version |
| 01h | `putc` | Write a character |
| 02h | `puts` | Write a string |
| 03h | `getc` | Read a key (blocking) |
| 04h | `thread_create` | Start a thread |
| 05h | `thread_exit` | End the calling thread |
| 06h | `yield` | Give up the CPU |
| 07h | `sleep` | Sleep for N ticks |
| 08h | `ticks` | Tick count in DX:AX |
| 09h–0Bh | `sem_create`, `sem_wait`, `sem_signal` | Semaphores |
| 0Ch–0Eh | `mbox_create`, `mbox_send`, `mbox_recv` | Mailboxes |
| 0Fh–10h | `alloc`, `free` | Far-arena memory |
| 11h–12h | `sem_destroy`, `mbox_destroy` | Release semaphores and mailboxes |
| 18h–1Bh | `open`, `read`, `close`, `exec` | Files and programs |

### 4.9 Drivers

* **Console:** direct writes to text video memory; scrolling, colour, cursor.
* **Keyboard:** scan-code set 1 from port 60h, modifier tracking, **pluggable keymaps** (US first; UK, DE, FR, and others as data tables).
* **Serial:** polled and buffered UART output for telemetry, optional input channel.
* **Disk (v0.6):** BIOS-independent floppy access is out of scope; the kernel uses a small real-mode BIOS INT 13h shim with interrupts managed, documented as such.

### 4.10 Storage and programs (v0.6)

* The boot floppy is a valid **FAT12** volume, so any host OS can add files to it.
* Read-only FAT12 driver: root directory, cluster chains, 8.3 names.
* Programs are files with a small header, text, data and a relocation table. Text is loaded into the far arena and runs in its own code segment. In the 0.x line a program's data lives in the kernel data segment and is relocated at load time, so every thread keeps DS = SS = kernel data; a separate data segment per program is a 1.x goal.
* An SDK (`sdk/`) provides headers, a C runtime stub, a linker script, and example programs.

### 4.11 Shell, debug monitor, panic

* Shell runs as an ordinary thread: `help`, `ps`, `memory`, `stats`, `interrupts`, `scheduler`, `registers`, `create`, `kill`, `nice`, `sleep`, `ls`, `run`, `bench`, `keymap`, `panic`, `reboot`, `about`.
* Debug monitor: memory dump, stack dump, thread table, scheduler queue, interrupt counts.
* Panic: captures the **live** register state at the fault, shows thread info, emits a telemetry fault record, halts.

---

## 5. The Observatory

### 5.1 Telemetry protocol v1

* Framed binary protocol over COM1, specified in `docs/telemetry.md` and versioned independently of the kernel.
* Frame: delimiter, protocol version, type, sequence number, tick timestamp, length, payload, CRC. Payload bytes are escaped so the delimiter never appears in data.
* **Console text is a packet type**, not raw bytes mixed into the stream.
* Record types: boot stage, thread created/exited/state-changed, context switch (with saved register frame), interrupt counters, memory summary, syscall, fault/panic, console output, benchmark result.
* Emission is buffered in a ring and drained outside interrupt handlers. When the ring is full, records are dropped and a drop counter is reported. Telemetry can be compiled out entirely.
* An optional input channel carries keystrokes from the dashboard to the kernel.

### 5.2 Bridge

* Python service: reads the serial stream (TCP socket, PTY, or file), validates frames, and republishes JSON over WebSocket.
* Records every session to a capture file and can **replay** captures at variable speed.
* Packaged with pinned dependencies and its own tests.

### 5.3 Dashboard

* Static web app, no build step required, **no external network dependencies** (fonts and assets are vendored).
* Panels: boot timeline, memory map, thread list and inspector, scheduler timeline, context-switch view with register diff, interrupt log and rate, console mirror with input, fault view, benchmark summary.
* Three explicit modes, always labelled on screen: **Live**, **Replay**, **Demo (simulated)**. Simulated data is never shown in Live mode.
* All user-visible strings come from locale files; layout supports right-to-left languages.
* Accessible: keyboard navigable, sufficient contrast, reduced-motion support.

### 5.4 Browser demo

A hosted page boots the real image in an in-browser x86 emulator and feeds the dashboard, so anyone can try the project with zero installation. This is the primary public showcase.

---

## 6. Quality Requirements

| Area | Requirement |
| --- | --- |
| Build | Zero compiler warnings; build fails on any image-size or region overflow |
| Instruction set | CI disassembles the image and fails on any non-8086 opcode |
| Unit tests | Host-compiled tests for allocator, scheduler selection, queues, keymaps, FAT12 parsing, telemetry codec |
| Integration tests | Headless emulator boots the image, drives the keyboard, and asserts on console and telemetry output for every shell command and subsystem |
| Stability | 1-hour soak test with thread create/kill churn and no leaks, stack-guard hits, or lost IRQs |
| Bridge / dashboard | Python tests for the codec; dashboard tested against recorded captures |
| Documentation | Every subsystem has a design page; every public function has a header comment |
| Performance | `bench` reports context-switch cost, IRQ latency, and allocator cost; results are tracked per release |

---

## 7. Open-Source Project Requirements

* **Licence:** permissive (MIT recommended — decision D2), with SPDX headers in every source file.
* **Repository:** public Git repository with protected main branch, CI on every pull request, and tagged semantic-versioned releases.
* **Community files:** `README.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`, issue and pull-request templates, a public roadmap, and labelled starter issues.
* **Releases:** each release publishes the floppy image, checksums, a telemetry capture, and release notes.
* **Documentation site:** architecture guide, build guide for three host platforms, syscall and telemetry references, and a set of **course labs** (for example: "add a syscall", "write a scheduler policy", "add a keymap").
* **Translations:** docs live under `docs/<lang>/`; translation status is tracked; English is canonical.
* **Naming:** the project name and logo are checked for trademark conflicts before public launch (decision D3).

---

## 8. Release Plan

| Release | Theme | Outcome |
| --- | --- | --- |
| v0.2 | Foundation | Repository, licence, CI, honest documentation, build guards |
| v0.3 | True 8086 | 16-bit toolchain, 8086-only image, new memory layout |
| v0.4 | Real kernel | Working preemption, blocking primitives, syscalls, shell |
| v0.5 | Observatory | Telemetry v1, bridge with replay, truthful dashboard |
| v0.6 | Programs | FAT12, program loader, SDK |
| v0.9 | Hardening | Full test suite, soak tests, fidelity emulator, docs site, i18n |
| **v1.0** | Public launch | Browser demo, labs, release artefacts, stable ABI |
| 1.x | Growth | Real-hardware support matrix, more keymaps and translations, extra schedulers, RAM disk, 360 KB image |

---

## 9. Definition of Done for v1.0

1. The image boots on QEMU and on an 8086-only emulator with no 186+/386 instructions present.
2. At least four threads run concurrently under timer preemption, and the shell stays responsive.
3. Sleep, semaphores, mutexes, and mailboxes block and wake correctly under test.
4. Every documented system call works from a separately compiled program loaded from the FAT12 disk.
5. Panic shows the real register state and is visible on the dashboard.
6. The dashboard in Live mode shows only kernel-reported data; Replay reproduces a recorded session exactly.
7. CI is green: build, opcode lint, unit tests, integration tests, soak test.
8. A newcomer can build and run the system from the README in under 15 minutes on Linux, macOS, or Windows.
9. The browser demo is live and the documentation site, including at least three labs, is published.
10. The syscall ABI and telemetry protocol are frozen and versioned.
