# Phoenix-8086 Architecture

A guide to how the system works and where each part lives in the source. For what the project aims to be, see the [specification](../projectdetails.md).

## Overview

```
 BIOS ─► Stage 1 (boot sector) ─► Stage 2 (loader) ─► Kernel
                                                         │
        ┌───────────────┬───────────────┬────────────────┼───────────────┐
   Interrupts       Scheduler        Memory          Storage         Telemetry
   isr.S            scheduler.c      memory.c        disk.c          telemetry.c
   interrupts.c     thread.c                         fat12.c         serial.c
   panic.c          sync.c, ipc.c                    exec.c
        └───────────────┴───────┬───────┴────────────────┴───────────────┘
                          System calls (syscall.c)
                                │
                     Shell and programs (shell.c, sdk/)
```

Everything runs in 16-bit real mode with 8086 instructions only. There is no memory protection: the "kernel" and its threads share one address space by convention, not enforcement.

## Memory layout

Defined in `include/layout.h` and `linker/kernel.ld`.

| Physical range | Contents |
| --- | --- |
| `00000–003FF` | Interrupt vector table |
| `00400–004FF` | BIOS data area |
| `07C00–07DFF` | Stage 1 |
| `07E00–085FF` | Stage 2 |
| `10000–1FFFF` | Kernel code segment (CS = `1000h`) |
| `20000–2FFFF` | Kernel data segment (DS = SS = `2000h`): data, BSS, thread stacks, near heap, kernel stack at the top |
| `30000–top` | Far arena, up to the memory size the BIOS reports |

Code and data are separate 64 KB segments, each linked at offset 0. Pointers in C are 16-bit offsets into the data segment. Anything outside it (video memory, the interrupt table, the far arena) is reached with a `__far` pointer built by `MK_FP` (`kernel/hal.h`).

Kernel threads run with DS = SS = the kernel data segment. A loaded program runs in two far segments of its own, one for code and one for data and stacks. Kernel C code always runs with SS = DS = the kernel data segment, which the compiler assumes; the interrupt stubs guarantee it by switching to a kernel stack when they interrupt a program.

## Boot

1. **Stage 1** (`boot/stage1.asm`, one sector) starts with a FAT12 parameter block, sets up a stack, and loads Stage 2 with BIOS `INT 13h`.
2. **Stage 2** (`boot/stage2.asm`) reads the first kernel sector, checks the image header's magic, works out how many sectors the image needs, loads them to `1000:0000`, verifies a checksum, and jumps to the kernel with the boot drive and memory size in registers.
3. **Kernel entry** (`kernel/entry.S`) copies the data image to the data segment, zeroes the BSS, sets the segments and stack, and calls `kernel_main`.
4. **`kernel_main`** (`kernel/kernel_main.c`) brings up the serial port, console, panic traps, memory, keyboard, scheduler, interrupts and file system, creates the shell and telemetry threads, and then becomes the idle thread.

The kernel image header (magic, sizes, checksum) is written by the linker script and `tools/mkimage.py`. The build fails if the image outgrows its segment.

## Threads and context switching

A thread is a stack plus a task control block (`kernel/tcb.h`). The TCB stores the saved stack pointer and bookkeeping; **the registers themselves live on the thread's stack**, in the frame an interrupt pushed there.

Every interrupt stub (`kernel/isr.S`) does the same thing:

1. push all registers on the current stack (this is `frame_t`);
2. load the kernel data segment into DS and ES;
3. if the interrupted code was a program (SS is not the kernel's), switch to this thread's kernel stack, copy the frame onto it, and note where the program's stack was;
4. call a C handler with a pointer to the saved context, `uint16_t handler(uint16_t context)`;
5. resume whatever context the handler returned: if it belongs to a program, copy the frame back to the program's stack and switch to it; then pop the registers and `IRET`.

If the handler returns the context it was given, the interrupted thread resumes. If it returns another thread's saved context, that thread resumes instead. That is the whole context switch; there is no separate switch routine.

Every thread has a 2 KB kernel stack. A kernel thread runs on it all the time. A program thread runs on a stack in its program's data segment and uses its kernel stack only while the kernel is working for it.

A new thread gets a fabricated context on its kernel stack (`create` in `kernel/thread.c`), so the first switch to it is no different from any other.

Thread 0 is the boot context itself. It is never created; `sched_init` adopts it, and it runs the idle loop (`HLT`).

### Scheduling

`kernel/scheduler.c`. The timer runs at 100 Hz.

* The highest effective priority among runnable threads runs.
* Threads of equal priority take turns, five ticks each.
* A thread kept waiting gains one effective priority level every ten ticks (aging) and drops back to its base priority when it runs, so nothing starves.
* On every switch the outgoing thread's kernel stack is checked: its guard word must be intact and its stack pointer must not be within 192 bytes of the bottom. A program thread's own stack is checked the same way, with a 64-byte red zone; its last stack pointer there is kept at the top of its kernel stack. A thread that fails either check is stopped and a `THREAD_FAULT` record is sent. Real mode cannot prevent the overflow; this notices it at the next switch.
* Every stack is filled with a pattern when its thread is created, so the deepest point it has reached can be found later by counting untouched words from the bottom (`thread_stack_peak`, the `stacks` command, and `THREAD_STATS`).

### Blocking

`kernel/sync.c`, `kernel/ipc.c`. A semaphore keeps a queue of waiting thread IDs. `sem_wait` on an unavailable semaphore marks the thread blocked and yields (`INT 81h`); `sem_signal` makes the oldest waiter ready. `sem_wait_timeout` gives up after a number of ticks. Mutexes, mailboxes and the keyboard buffer are built on semaphores. Critical sections use `hal_irq_save` / `hal_irq_restore`, which nest and work inside interrupt handlers.

Mutexes use **priority inheritance**. A thread that blocks on a mutex lends its effective priority to the owner, and on along the chain if the owner is waiting for another mutex (up to four levels). The loan is kept in the TCB's `inherited` field, which the scheduler keeps when it drops an aging boost. As in FreeRTOS, an owner keeps what it inherited until it has released every mutex it holds, then yields so the thread it stood in for can run. Aging alone would let the owner back in after roughly 40–50 ticks; with inheritance the waiting thread is held up only for the critical section. The self-test measures exactly that.

## Interrupts

| Vector | Source | Handler |
| --- | --- | --- |
| 00h, 01h, 03h, 04h | CPU traps (divide error, single step, breakpoint, overflow) | Panic with the live registers |
| 08h | Timer (IRQ0) | Tick accounting, then the scheduler |
| 09h | Keyboard (IRQ1) | Scan code → layout → buffer, then the scheduler |
| 0Eh | Floppy controller (IRQ6) | Wakes the thread waiting for the drive |
| 80h | System call | `syscall_dispatch` |
| 81h | Yield | The scheduler |
| 82h | `kernel_panic()` | Captures registers for the panic screen |

A system call handler runs on the calling thread's own stack, so a call that must wait simply calls the blocking kernel function; the thread is switched out mid-call and finishes when woken. Arguments from a program are checked first: pointers against its own data segment, entry points against its code, handles against a table of the objects the kernel handed out, segments to free against their owner (see [Argument checks](syscalls.md#argument-checks)).

## Memory management

`kernel/memory.c`.

* **Near heap:** a first-fit free list with coalescing, inside the data segment between the end of BSS and the kernel stack. `kmalloc` / `kfree`.
* **Far arena:** paragraph-granular (16 bytes) blocks above the kernel segments, each preceded by a one-paragraph header. `far_alloc` returns a segment.

## Storage and programs

* **Disk** (`kernel/disk.c`, `kernel/floppy.c`): the kernel drives the floppy controller itself, with DMA channel 2 and IRQ6; a thread that reads a sector waits on a semaphore the interrupt handler signals, so other threads run meanwhile. If no controller answers, the kernel falls back to BIOS `INT 13h`, handing the timer and keyboard interrupts back to the BIOS for each read; nothing is scheduled during a BIOS read.
* **File system** (`kernel/fat12.c`): read-only FAT12, root directory only. The FAT is loaded whole at mount.
* **Programs** (`kernel/exec.c`, `sdk/`): a program's code is loaded into one far segment and its data, bss and thread stacks into another. System calls reach the program's memory through its saved DS. See [Writing programs](programs.md).

## Console, keyboard, serial

* **Console** (`kernel/console.c`): writes character and attribute pairs directly to text video memory at `B800:0000`.
* **Keyboard** (`kernel/keyboard.c`): scan code set 1. A layout is the US table plus a list of the keys that differ; four layouts are built in.
* **Serial** (`kernel/serial.c`): polled COM1, used by telemetry in both directions.

## Telemetry

`kernel/telemetry.c`; protocol in [telemetry.md](telemetry.md).

Events are appended to a ring buffer as small records. A telemetry thread wakes ten times a second, frames the records with a CRC, and sends them over COM1. No serial I/O happens in an interrupt handler. If the buffer fills, records are dropped and counted, and the count is reported.

On the host, `bridge/` decodes the stream and relays it to the dashboard over WebSocket, or the dashboard decodes it itself when the kernel runs in the page (`dashboard/protocol.js`, `dashboard/browser.js`). The dashboard's state is a pure function of the messages it has received (`dashboard/model.js`).

## How it is tested

| Check | What it shows | Command |
| --- | --- | --- |
| Instruction-set check | No instruction newer than the 8086 in the kernel or the example programs | `make check` |
| In-kernel self-test | Allocators, semaphores, mailboxes, sleep, system calls, layouts, file system, loader | `selftest` at the shell |
| Integration test | The shell, scheduling, IPC, programs, panics and telemetry under QEMU | `make test` |
| 8086 fidelity test | The same kernel on an emulated 8086 | `make test-8086` |
| Soak test | No leaks or faults under sustained churn | `make soak` |
| Size budget | Neither 64 KB segment is more than 90% full; largest functions and variables | `make size` |
| Host unit tests | Protocol decoders, bridge, dashboard model and page code | part of `make test` |
