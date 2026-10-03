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

### Changed
* Memory layout: kernel code at `1000:0000`, kernel data and stacks at `2000:0000`.
* Thread stacks are 2 KB each and live inside the kernel data segment.
* `phoenix.sh` no longer kills other processes; it reports busy ports and stops.

### Fixed
* Kernel image was silently truncated when it grew past 32 sectors.
* Thread stacks overlapped the interrupt vector table.
* Stage 2 overlapped the kernel load address.
* Sleep stopped working after 65,536 ticks.

### Removed
* Unused NASM duplicates of the kernel assembly (archived in `docs/archive/legacy/`).
* `tools/build.sh` and `tools/run.sh` (use `make` and `phoenix.sh`).

## 0.1 — prototype

Initial prototype: boot chain, console, and interrupt plumbing. Threads did not run.
