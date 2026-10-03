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
* About 2 KB of stack. Keep large arrays `static`.
* Only 8086 instructions; `make check` verifies every example program.

## How a program is loaded

| Part | Where it goes | Notes |
| --- | --- | --- |
| Code | Its own segment in far memory | Runs with CS pointing there, so code offsets need no adjustment |
| Data and constants | A block on the kernel's near heap | Followed by the zeroed bss |
| Stack | The thread's stack | Same as for kernel threads |

A program runs with DS, ES and SS pointing at the kernel data segment, like every other thread. That keeps the interrupt and system-call paths identical for kernel threads and programs, and it means a pointer passed to a system call is an ordinary pointer.

The price is **relocation**: the data block can land anywhere on the heap, so the program file lists every place that holds a data address, and the loader adds the block's address to each one. Code addresses are not relocated.

When the program's thread ends, by returning from `main`, calling `px_exit`, or being killed, the kernel frees its code, its data, and any files it left open.

### Limits

* Programs share the kernel's 64 KB data segment. A program's data and bss come out of the same heap the kernel uses (about 30 KB free).
* There is no memory protection in real mode. A program can overwrite the kernel or another program.
* `thread_create` (call 04h) takes a code offset in the kernel's segment, so a program cannot use it to start a thread in its own code. Use `px_exec` to start another program.
* Programs are found in the root directory only, by 8.3 name.

## File format

Built by `sdk/mkprog.py`; loaded by `kernel/exec.c`. All fields are little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `PXE1` |
| 4 | 2 | Text size in bytes |
| 6 | 2 | Data size in bytes (initialised data and constants) |
| 8 | 2 | Bss size in bytes |
| 10 | 2 | Entry point, an offset into the text |
| 12 | 2 | Number of text relocations |
| 14 | 2 | Number of data relocations |
| 16 | … | Text, then data, then the text relocations, then the data relocations |

A relocation is the 16-bit offset of a word, inside the text or inside the data, that holds a data address. The loader rejects a file whose sizes do not add up to the file length or whose relocations point outside their region.

## Disk access

The kernel reads the boot drive through the BIOS (`INT 13h`). For each sector it hands the timer and keyboard interrupts back to the BIOS, makes the call, and takes them again. During a read nothing is scheduled, the tick counter stops, and keys pressed are lost. A native floppy driver would remove those limits and is future work. The file system is read-only FAT12.
