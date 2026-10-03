# Changelog

All notable changes to this project are recorded here. The project follows [Semantic Versioning](https://semver.org/); nothing is stable before 1.0.

## Unreleased

### Added
* True 8086 build with `ia16-elf-gcc`; `make toolchain` fetches the compiler without root.
* `make check`: fails the build if the kernel contains a non-8086 instruction.
* `make test`: boot smoke test and shell integration tests in headless QEMU.
* Kernel image header with size and checksum, verified by the Stage 2 loader.
* Real context switching: threads, the shell, and the idle thread actually run.
* Priority scheduler with time slices and aging; 100 Hz timer.
* Blocking semaphores, mutexes, and mailboxes; `mbox_broadcast` and non-blocking variants.
* System calls 00h–10h through `INT 80h`, documented in `docs/syscalls.md`.
* Far-memory allocator for the memory above the kernel segments.
* Panic screen with live registers; traps for divide error, single-step, breakpoint, and overflow.
* Stack red zone: a thread is stopped before a stack overflow reaches another stack.
* Shell commands: `ipc`, `syscall`, `nice`, `sleep`, `bench`, `selftest`, `overflow`, `divzero`.
* README, contribution guide, and continuous integration.
* Telemetry protocol v1 (`docs/telemetry.md`): framed, CRC-checked records buffered in the kernel and sent by a telemetry thread; console text is a record type; dropped records are counted.
* New telemetry records: thread create/exit/state, context switch with registers, counters, memory, system calls, faults, benchmark results, per-thread statistics.
* Serial input: the dashboard can type into the shell.
* Bridge: capture to file, replay, backlog for late-joining dashboards.
* Dashboard: Live / Replay / Demo / Offline modes that are always labelled; scheduler timeline, CPU shares, registers, memory map and thread inspector driven only by kernel data.
* `tools/record_session.py` records a kernel session to a capture file.
* `make TELEMETRY=0` builds a kernel without telemetry.
* `make test-8086`: boots and drives the kernel on DOSBox-X configured as an 8086.
* `cpu` shell command: run-time processor family detection.
* The boot floppy is a FAT12 volume (`tools/mkfat12.py`); Stage 2 and the kernel live in its reserved sectors.
* Read-only FAT12 driver and BIOS disk access; `ls` and `cat` shell commands.
* Program loader with load-time relocation; `run` shell command; programs free their memory and files on exit.
* SDK (`sdk/`): `phoenix.h`, startup code, linker script, `mkprog.py`, and three example programs.
* System calls 18h–1Bh: `open`, `read`, `close`, `exec`.
* `tools/test_image.py`: independent check of the floppy image and program files.
* `make soak`: sustained churn of threads, IPC and program loads with leak and fault checks; nightly one-hour workflow.
* Keyboard layouts (US, UK, German, French) with AltGr and Caps Lock; `keymap` shell command.
* Dashboard translations (English, German, French, Spanish, Arabic) with a language switcher and right-to-left support.
* In-browser demo: the dashboard can boot the kernel in the v86 emulator inside the page; `tools/build_site.sh`, a JavaScript telemetry decoder, and a GitHub Pages workflow.
* Release workflow: a version tag builds, tests and publishes the image, checksum and a telemetry capture.
* Documentation: architecture guide, build guide, translation guide, three labs.
* `tools/test_dashboard_browser.mjs`: the dashboard and the in-browser demo tested in headless Chromium, with screenshots kept by CI.

### Changed
* Memory layout: kernel code at `1000:0000`, kernel data and stacks at `2000:0000`.
* Thread stacks are 2 KB each and live inside the kernel data segment.
* `phoenix.sh` no longer kills other processes; it reports busy ports and stops.

### Fixed
* Dashboard layout problems found in a real browser: the page could grow taller than the window and hide the bottom bar; the console view overflowed its panel; the context-switch steps wrapped; emoji icons rendered as empty boxes; list entries stayed half-faded while data was flowing; letter-spacing broke Arabic text apart.
* The dashboard's "hide telemetry thread" filter missed switches out of that thread.
* Dashboard no longer falls back to random data or draws random bar heights.
* Telemetry no longer does serial I/O inside the timer interrupt, and no longer reports context switches that did not happen.
* Kernel image was silently truncated when it grew past 32 sectors.
* Thread stacks overlapped the interrupt vector table.
* Stage 2 overlapped the kernel load address.
* Sleep stopped working after 65,536 ticks.

### Removed
* Unused NASM duplicates of the kernel assembly (archived in `docs/archive/legacy/`).
* `tools/build.sh` and `tools/run.sh` (use `make` and `phoenix.sh`).

## 0.1 — prototype

Initial prototype: boot chain, console, and interrupt plumbing. Threads did not run.
