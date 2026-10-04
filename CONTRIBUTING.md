# Contributing to Phoenix-8086

Thanks for your interest. The project is pre-alpha; the [implementation plan](implementation_plan.md) lists what is open, and the [specification](projectdetails.md) says what the project is aiming for.

## Before you open a pull request

1. Build and test: `make test` must pass. It checks that the kernel contains only 8086 instructions, runs the host-compiled kernel tests under sanitizers, boots the image in QEMU, and drives the shell. If you change `sync.c`, `ipc.c`, `memory.c` or `fat12.c`, add a case to `tests/host/` too: it is much quicker to debug there than on the target.
2. Keep the build free of compiler warnings.
3. If you add or change kernel behaviour, add a check for it: an assertion in `kernel/selftest.c` for logic, or a step in `tools/integration_test.py` for behaviour visible at the shell.
4. If you change a system call or the memory layout, update `docs/syscalls.md` or `include/layout.h` in the same change. `boot/stage2.asm` carries its own copy of the load segment and header offsets.
5. If you change what a screen looks like, regenerate the pictures that show it with `tools/screenshot.py` (see `docs/building.md`, "Screenshots"), and update any sample output in `README.md` and `docs/manual.md`. Samples there are copied from real runs, not written by hand.

## Rules for kernel code

* **8086 only.** No 186+ instructions in assembly (`pusha`, `push imm`, shifts by an immediate other than 1). `make check` enforces this for the kernel, and NASM's `CPU 8086` for the boot sectors.
* **Pointers are 16 bits** and relative to the kernel data segment. Memory outside it (video memory, the interrupt table, far allocations) needs a `__far` pointer; see `MK_FP` in `kernel/hal.h`.
* **Critical sections** use `hal_irq_save()` / `hal_irq_restore()`, never bare `cli` / `sti`, so they nest and work inside interrupt handlers.
* **Thread stacks are 2 KB.** Avoid large local arrays.
* **Known compiler pitfalls:** `ia16-elf-gcc` 6.3 at `-Os` miscompiles calls like `f((uint8_t)(x >> 8))` when `x` is 32 bits wide, and can drop the store when a 32-bit function result is narrowed to a byte. Split the value into a byte array first (see `put32` in `kernel/telemetry.c`), or go through a 16-bit variable (see `px_getc` in `sdk/include/phoenix.h`).
* **Screen output** goes through the UI toolkit (`kernel/ui.h`): panels, rows, meters, marks. Ask for colour *roles* (`TH_TEXT`, `TH_GREEN`, ...) and glyphs (`GLYPH(G_CHECK)`), never raw colour numbers or character codes, so the screen works on both a VGA and a CGA. Keep panel text inside the 78-column panel, and check the CGA look with `make CONSOLE=plain`.
* **Tests read the screen text.** `tools/integration_test.py` and the other drivers match patterns in the console output that telemetry carries. If you change the wording or layout of a message, run `make test` and update the patterns in the same change.
* Match the style of the file you are editing. New source files start with an `SPDX-License-Identifier: MIT` line.

## Good first tasks

* Review one of the dashboard translations if you are a native speaker (`docs/translating.md`).
* Add a keyboard layout (`docs/labs/03-keyboard-layout.md`).
* Try the dashboard in Firefox or Safari, or with a screen reader, and report what is wrong; only Chromium is tested.
* Try the build on macOS or another Linux distribution and update the table in `docs/building.md`.
* Boot the image on DOSBox-X with a CGA (`make test-8086`) and report anything on screen that looks wrong; the CGA fallback of the console is the least tested part of it.
* Work through one of the labs as a newcomer and report where the instructions were unclear.

## Reporting bugs

Open an issue with what you ran, what you expected, and what happened. For kernel bugs, the output of `make test` and the text on the QEMU screen help most.

## Licence

By contributing you agree that your contribution is licensed under the [MIT License](LICENSE).
