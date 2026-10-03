# Writing Programs for Phoenix-8086

A program is a separately built file on the boot disk that the kernel loads and runs as a thread. The SDK in `sdk/` has everything needed to build one.

## Quick start

```c
/* sdk/examples/hello.c */
#include "phoenix.h"

int main(void)
{
    px_puts("Hello from a program loaded off the disk!\n");
    return 0;
}
```

Add the name to `PROGRAM_NAMES` in the `Makefile`, run `make`, boot, and type `run hello.bin`. The build compiles the program, links it with `sdk/program.ld`, converts it with `sdk/mkprog.py`, and copies it onto the floppy image.

Because the floppy is a FAT12 volume, you can also copy a program onto an existing image with any FAT tool, for example `mcopy -i build/phoenix8086.img MYPROG.BIN ::`.

## What a program can use

* `sdk/include/phoenix.h` — one wrapper per system call: console, sleep and ticks, semaphores, mailboxes, far memory, files, and starting other programs. [docs/syscalls.md](syscalls.md) describes the calls.
* No C library. `int` is 16 bits, `long` is 32 bits, pointers are 16 bits.
* About 2 KB of stack per thread. Keep large arrays `static`.
* Only 8086 instructions; `make check` verifies every example program.

## How a program is loaded

| Part | Where it goes | Notes |
| --- | --- | --- |
| Code | A code segment of its own in far memory | The thread runs with CS pointing there |
| Data, constants and bss | A data segment of its own in far memory | The thread runs with DS, ES and SS pointing there. Data starts at offset 16; the bytes below it are unused, so a null pointer never points at anything |
| Stacks | After the bss, in the same data segment | 2 KB for each of up to four threads |

Because both parts are linked at fixed offsets inside their own segments, nothing has to be adjusted when the program is loaded, and the program's pointers are plain 16-bit offsets into its own data segment. A program can use up to about 56 KB of data and bss, independent of the kernel's heap; `sdk/examples/where.c` prints where it was loaded and uses 50 KB.

### Calling the kernel

A pointer a program passes to a system call is an offset in the *program's* data segment. The kernel finds it through the caller's saved DS, so strings and buffers do not need to be anywhere special.

When an interrupt or a system call arrives while a program is running, the processor is on the program's stack, in the program's segment. The kernel's handlers are C code and need the kernel's segment, so the interrupt stub switches to a kernel stack that every thread has, copies the saved registers there, and switches back on the way out (`kernel/isr.S`).

### Threads

A program can start more threads in its own code with `px_thread_create` (see `sdk/examples/threads.c`). Each gets one of the program's stacks. A thread ends by returning from its function, calling `px_exit`, or being killed; the kernel then closes the files it left open and frees the far memory it allocated. The program's code and data are freed when its last thread has ended, so worker threads may outlive `main`.

### Limits

* There is no memory protection in real mode. A program has its own segments, but nothing stops it writing outside them.
* At most four threads per program, each with a 2 KB stack. An overflow of a program's stack is not detected (an overflow of a kernel stack is).
* One object cannot exceed 32,767 bytes with this compiler.
* Far memory from `px_alloc` belongs to the thread that allocated it.
* Programs are found in the root directory only, by 8.3 name.

## File format

Built by `sdk/mkprog.py`; loaded by `kernel/exec.c`. All fields are little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `PXE2` |
| 4 | 2 | Text size in bytes |
| 6 | 2 | Data size in bytes (initialised data and constants) |
| 8 | 2 | Bss size in bytes |
| 10 | 2 | Entry point, an offset into the text |
| 12 | 2 | Offset of the data within the data segment (16) |
| 14 | 2 | Reserved, zero |
| 16 | … | Text, then data |

The loader rejects a file whose sizes do not add up to the file length, or whose data, bss and stacks would not fit in one segment.

## Disk access

The kernel reads the boot drive with one of two drivers, chosen at boot; `disk` at the shell shows which, and `disk native` or `disk bios` switches.

* **Native floppy driver** (`kernel/floppy.c`): the kernel programs the floppy controller, the DMA controller and IRQ6 itself. A thread that reads the disk sleeps while the drive works and everything else keeps running. Used when the machine booted from drive A: and a controller answers.
* **BIOS** (`INT 13h`): the fallback. For each sector the kernel hands the timer and keyboard interrupts back to the BIOS, makes the call, and takes them again. During a read nothing is scheduled, the tick counter stops, and keys pressed are lost.

QEMU and v86 get the native driver. DOSBox-X does not emulate a floppy controller for a booted image, so it gets the BIOS driver. The file system is read-only FAT12.
