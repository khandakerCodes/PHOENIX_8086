# Phoenix-8086 Architecture

A guide to how the system works and where each part lives in the source. For what the project aims to be, see the [specification](../projectdetails.md).

## Overview

```
 BIOS ─► Stage 1 (boot sector) ─► Stage 2 (loader) ─► Kernel
                                                         │
     ┌────────────┬─────────────┬───────────┬────────────┼────────────┬────────────┐
 Interrupts   Scheduler      Memory      Storage      Console      Telemetry
 isr.S        scheduler.c    memory.c    disk.c       console.c    telemetry.c
 interrupts.c thread.c                   floppy.c     ui.c         serial.c
 panic.c      sync.c, ipc.c              fat12.c      keyboard.c
                                         exec.c
     └────────────┴──────┬──────┴───────────┴────────────┴────────────┘
                   System calls (syscall.c)
                         │
              Shell and programs (shell.c, debug.c, sdk/)
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

1. **Stage 1** (`boot/stage1.asm`, one sector, loaded by the BIOS to `0000:7C00`) starts with a FAT12 parameter block, sets up a stack, and loads Stage 2, the next four sectors, to `0000:7E00` with BIOS `INT 13h`. It passes the boot drive in `DL`.
2. **Stage 2** (`boot/stage2.asm`) asks the BIOS for the size of conventional memory (`INT 12h`), reads the first kernel sector (LBA 5), checks the image header's magic `PX86`, works out how many sectors the image needs, loads them to `1000:0000`, verifies the 16-bit checksum, and jumps to `1000:0010` with the boot drive in `DL` and the memory size in kilobytes in `CX`.
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

`kernel/scheduler.c`. The timer runs at 100 Hz. The timer chip is in mode 2 (rate generator), so its counter runs down once per tick and reading it (`timer_counts` in `kernel/interrupts.c`) tells how far into the tick the kernel is, to about 0.84 µs; context-switch telemetry and the `bench` interrupt-latency figure use this. During a BIOS disk read the BIOS gets its own mode 3 back.

The scheduler runs whenever an interrupt handler calls `sched_switch`: on every timer tick, every keyboard and floppy interrupt, every yield (`INT 81h`), and after a system call that sleeps, yields or ends the thread. A call that has to wait, such as `sem_wait`, yields from inside the kernel. Each time, it applies these rules:

* Each thread has a base priority (0-255, higher first) and an effective priority, which is what is compared.
* The runnable thread with the highest effective priority runs. Among equals the search starts after the current thread in TID order, so they take turns (round robin).
* The running thread keeps the processor until its time slice of five ticks is used up, unless a thread with a strictly higher effective priority is ready.
* A thread kept waiting in the READY state gains one effective priority level every ten ticks (aging). When it is scheduled its effective priority drops back to its base priority, or to a priority inherited through a mutex if that is higher (see Blocking). So nothing starves.
* The idle thread (TID 0) runs only when nothing else is runnable, and never ages.
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

* **Console** (`kernel/console.c`): writes character and attribute pairs directly to text video memory at `B800:0000`. Rows 0-23 scroll; row 24 is the status bar. At start-up it asks the BIOS (`INT 10h`, `AX=1A00h`) whether the display is a VGA. If so it loads the Catppuccin Mocha theme into the DAC palette, turns blinking off so all 16 colours work as backgrounds, and writes 20 UI glyphs (rounded corners, pill ends, powerline arrows, a thick meter bar, sparkline levels, check and cross marks) into the font in plane 2 of video memory. Glyphs that must join the next cell use codes `C0h`-`DFh`, which a VGA extends into the ninth pixel column. Code asks for colour *roles* (`TH_*`) and glyphs (`GLYPH(G_*)`), never raw colours or character codes, so on a CGA the same calls give the standard colours and code-page-437 characters. Text sent to telemetry is always standard CP437, and the prompt and status bar are not sent at all, so the dashboard and the tests see plain text. `make CONSOLE=plain` forces the CGA look on a VGA.
* **UI toolkit** (`kernel/ui.c`): the pieces every screen is drawn with: panels with a title and a note, table rules, meters, pills, coloured state dots, check and cross marks, the powerline prompt, the boot checklist and the logo. The status bar is redrawn once a second from the timer interrupt, straight into row 24, and shows uptime, threads, free memory, an eight-second load sparkline (from the idle thread's CPU ticks) and the detected processor.
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
| Host-compiled kernel tests | `sync.c`, `ipc.c`, `memory.c` and `fat12.c` built unmodified for the host (`tests/host/`), under AddressSanitizer and UBSan: inheritance chains, allocator invariants under random load, every file on the real floppy image, and a FAT12 fuzzer | `make test-host`, `make coverage` |
| Integration test | The shell, scheduling, IPC, programs, panics and telemetry under QEMU | `make test` |
| 8086 fidelity test | The same kernel on an emulated 8086 | `make test-8086` |
| Soak test | No leaks or faults under sustained churn | `make soak` |
| Browser tests | The dashboard and the in-browser kernel in headless Chromium, with an accessibility audit | `node tools/test_dashboard_browser.mjs`, `node tools/test_browser_demo.mjs site` |
| Size budget | Neither 64 KB segment is more than 90% full; largest functions and variables | `make size` |
| Host unit tests | Protocol decoders, bridge, dashboard model and page code | part of `make test` |
