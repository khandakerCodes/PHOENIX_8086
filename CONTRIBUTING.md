# Contributing to Phoenix-8086

Thanks for your interest. The project is pre-alpha; the [implementation plan](implementation_plan.md) lists what is open, and the [specification](projectdetails.md) says what the project is aiming for.

## Before you open a pull request

1. Build and test: `make test` must pass. It checks that the kernel contains only 8086 instructions, boots the image in QEMU, and drives the shell.
2. Keep the build free of compiler warnings.
3. If you add or change kernel behaviour, add a check for it: an assertion in `kernel/selftest.c` for logic, or a step in `tools/integration_test.py` for behaviour visible at the shell.
4. If you change a system call or the memory layout, update `docs/syscalls.md` or `include/layout.h` in the same change. `boot/stage2.asm` carries its own copy of the load segment and header offsets.

## Rules for kernel code

* **8086 only.** No 186+ instructions in assembly (`pusha`, `push imm`, shifts by an immediate other than 1). `make check` enforces this for the kernel, and NASM's `CPU 8086` for the boot sectors.
* **Pointers are 16 bits** and relative to the kernel data segment. Memory outside it (video memory, the interrupt table, far allocations) needs a `__far` pointer; see `MK_FP` in `kernel/hal.h`.
* **Critical sections** use `hal_irq_save()` / `hal_irq_restore()`, never bare `cli` / `sti`, so they nest and work inside interrupt handlers.
* **Thread stacks are 2 KB.** Avoid large local arrays.
* Match the style of the file you are editing. New source files start with an `SPDX-License-Identifier: MIT` line.

## Reporting bugs

Open an issue with what you ran, what you expected, and what happened. For kernel bugs, the output of `make test` and the text on the QEMU screen help most.

## Licence

By contributing you agree that your contribution is licensed under the [MIT License](LICENSE).
