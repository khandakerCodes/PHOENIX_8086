# Lab 1 — Add a System Call

## Goal

Add a system call that returns the calling thread's ID, and use it from a program loaded off the disk.

You will learn how a request travels from a program to the kernel and back: the software interrupt, the saved register frame, and the dispatcher.

## Background

A program calls the kernel with `INT 80h`, function number in AH. The interrupt stub in `kernel/isr.S` saves every register, moves that block (the *frame*) onto the thread's kernel stack if the caller was a program, and passes it to `syscall_handler` in `kernel/interrupts.c`, which calls `syscall_dispatch` in `kernel/syscall.c`.

The dispatcher reads its arguments from the frame (`frame->bx`, `frame->cx`, ...) and writes its result into `frame->ax`. When the stub pops the registers and returns, the caller finds that value in AX. Setting `error = true` makes the caller see the carry flag set and AX = FFFFh.

Read `docs/syscalls.md` and the `SYS_VERSION` and `SYS_TICKS` cases in `kernel/syscall.c` before you start.

## Steps

1. **Choose a number.** In `kernel/syscall.h`, add `SYS_GETTID` with the first unused number after `SYS_MBOX_DESTROY`.
2. **Implement it.** In `syscall_dispatch`, add a case that puts the current thread's ID in `frame->ax`. `thread_current_tid()` gives you the ID.
3. **Expose it to programs.** In `sdk/include/phoenix.h`, add a `px_gettid()` wrapper next to `px_version()`.
4. **Use it.** Copy `sdk/examples/hello.c` to `sdk/examples/whoami.c`. Make it print `I am thread N`. Add `whoami` to `PROGRAM_NAMES` in the `Makefile`.
5. **Document it.** Add a row to the table in `docs/syscalls.md`.
6. **Test it in the kernel.** In `test_syscalls` in `kernel/selftest.c`, add an `expect` that the call returns `thread_current_tid()`.

## Check

```sh
make test
make run
```

At the prompt:

```
phoenix> run whoami.bin
  ✓ Started whoami.bin as TID=3
I am thread 3
phoenix> selftest
```

The number the program prints must match the `TID=` the shell reported, and `selftest` must report `0 failed`.

## Questions

1. The handler runs with interrupts off. Does `thread_current_tid()` need any further protection here? Why or why not?
2. Run `whoami.bin` twice in a row. Is the ID the same? Explain what you see using `thread_create` in `kernel/thread.c`.
3. What would happen if your case forgot to set `frame->ax`? Try it. Where does the value the program sees come from?
4. `px_gettid()` returns `unsigned`. The kernel's `thread_current_tid()` returns `int` and can return -1. Can a program ever see that?

## Going further

Add `SYS_GETPRIORITY` and `SYS_SETPRIORITY`. Decide what a thread should be allowed to do to another thread's priority, and say why, given that real mode cannot enforce it.
