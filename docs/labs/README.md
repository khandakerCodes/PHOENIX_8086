# Labs

Guided exercises on the Phoenix-8086 kernel, for a course or for self-study. Each lab states what you will learn, gives the steps, and ends with a check that tells you whether it worked.

| Lab | Topic | You change | Time |
| --- | --- | --- | --- |
| [1. Add a system call](01-system-call.md) | How a program asks the kernel for a service | `kernel/syscall.c`, the SDK header, a program | 1–2 hours |
| [2. Change the scheduler](02-scheduler.md) | Priorities, time slices and starvation | `kernel/scheduler.c` | 2–3 hours |
| [3. Add a keyboard layout](03-keyboard-layout.md) | Scan codes and device drivers | `kernel/keyboard.c` | 1 hour |

## Before you start

Build and test the unmodified kernel first ([Building and running](../building.md)):

```sh
make toolchain
make test
```

Read the [architecture guide](../architecture.md), at least the sections on threads and interrupts.

## Ground rules for kernel code

* 8086 instructions only. `make check` tells you if something newer slipped in.
* Pointers are 16 bits; `int` is 16 bits; `long` is 32 bits.
* Thread stacks are 2 KB. Make large arrays `static`.
* Protect shared data with `hal_irq_save()` / `hal_irq_restore()`.
* `make test` must still pass when you are done, unless the lab says a specific check is expected to change.

Worked solutions are in [solutions/](solutions/README.md). Try the lab first.
