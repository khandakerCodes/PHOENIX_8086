# Phoenix-8086 System Calls

ABI version 1 (not frozen until v0.9). The definitions live in `kernel/syscall.h`; the dispatcher is `kernel/syscall.c`.

## Calling convention

```
mov ah, <function>
; arguments in AL, BX, CX, DX
int 80h
; result in AX (DX:AX for 32-bit results)
; carry flag clear = success, set = error (AX = FFFFh)
```

All other registers are preserved. Pointers are near pointers in the kernel data segment, which is also where a loaded program's data lives; real mode has no memory protection, so the kernel does not validate them beyond a NULL check.

A call marked **blocks** may suspend the calling thread until it can complete. The thread is switched out in the middle of the call and resumes there when woken.

## Table

| AH | Name | Arguments | Result | Notes |
| --- | --- | --- | --- | --- |
| 00h | `version` | — | AX = ABI version | |
| 01h | `putc` | AL = character | — | |
| 02h | `puts` | BX = string pointer | — | Null-terminated |
| 03h | `getc` | — | AL = character | Blocks until a key is pressed |
| 04h | `thread_create` | BX = entry address, CL = priority | AX = thread ID | Error if no free slot |
| 05h | `thread_exit` | — | does not return | |
| 06h | `yield` | — | — | Gives up the rest of the time slice |
| 07h | `sleep` | CX = ticks | — | Blocks; 100 ticks per second |
| 08h | `ticks` | — | DX:AX = ticks since boot | |
| 09h | `sem_create` | BX = initial count | AX = semaphore handle | Error if out of memory |
| 0Ah | `sem_wait` | BX = handle | — | Blocks while the count is zero |
| 0Bh | `sem_signal` | BX = handle | — | Wakes the oldest waiter |
| 0Ch | `mbox_create` | — | AX = mailbox handle | 16 messages of 16 bits |
| 0Dh | `mbox_send` | BX = handle, CX = message | — | Blocks while the mailbox is full |
| 0Eh | `mbox_recv` | BX = handle | AX = message | Blocks while the mailbox is empty |
| 0Fh | `alloc` | BX = paragraphs (16 bytes each) | AX = segment | Far memory; data at segment:0000 |
| 10h | `free` | BX = segment | — | |
| 18h | `open` | BX = file name pointer | AX = file handle | Root directory of the boot disk, read-only; at most 4 files open system-wide |
| 19h | `read` | BX = handle, CX = length, DX = buffer pointer | AX = bytes read | 0 at end of file; may stop the scheduler briefly (see docs/programs.md) |
| 1Ah | `close` | BX = handle | — | Files are also closed when their thread ends |
| 1Bh | `exec` | BX = program file name pointer | AX = thread ID | Loads and starts a program; see docs/programs.md |

## Known gaps

* There is no call to destroy a semaphore or mailbox yet.
* Far memory a thread allocated with `alloc` is not released when it exits. (A loaded program's own code and data are.)
* `thread_create` takes an entry address in the kernel's code segment, so programs cannot use it; they start other programs with `exec`.
* `mbox_recv` cannot report an error in AX, because every 16-bit value is a valid message; check the carry flag.

## Example

The `syscall` shell command runs a thread that uses only `INT 80h` (`demo_syscall` in `kernel/shell.c`). The `selftest` command exercises calls 00h, 09h–10h and 18h–1Bh. The programs in `sdk/examples/` use the calls through `sdk/include/phoenix.h`.
