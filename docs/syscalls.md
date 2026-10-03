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

All other registers are preserved. Pointers are near pointers in the kernel data segment; real mode has no memory protection, so the kernel does not validate them beyond a NULL check.

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
| 18h–1Bh | `open`, `read`, `close`, `exec` | | | Reserved for v0.6 |

## Known gaps

* There is no call to destroy a semaphore or mailbox yet.
* A thread that exits while owning far memory does not release it.
* `mbox_recv` cannot report an error in AX, because every 16-bit value is a valid message; check the carry flag.

## Example

The `syscall` shell command runs a thread that uses only `INT 80h` (`demo_syscall` in `kernel/shell.c`). The `selftest` command exercises calls 00h and 09h–10h.
