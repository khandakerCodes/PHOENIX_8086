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

All other registers are preserved. A pointer argument is an offset in the *caller's* data segment; the kernel reaches it through the caller's saved DS. Arguments from a loaded program are checked before use (see [Argument checks](#argument-checks)).

A call marked **blocks** may suspend the calling thread until it can complete. The thread is switched out in the middle of the call and resumes there when woken.

## Table

| AH | Name | Arguments | Result | Notes |
| --- | --- | --- | --- | --- |
| 00h | `version` | — | AX = ABI version | |
| 01h | `putc` | AL = character | — | |
| 02h | `puts` | BX = string pointer | — | Null-terminated |
| 03h | `getc` | — | AL = character | Blocks until a key is pressed. If several threads are waiting, the key goes to the one that asked last |
| 04h | `thread_create` | BX = entry address in the caller's code segment, CL = priority, DX = value for the new thread's SI | AX = thread ID | Error if no free slot. In a program, use `px_thread_create` from the SDK, which routes the new thread through a small startup stub |
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
| 0Fh | `alloc` | BX = paragraphs (16 bytes each) | AX = segment | Far memory; data at segment:0000; owned by the calling thread |
| 10h | `free` | BX = segment | — | Far memory still allocated when a thread ends is reclaimed |
| 11h | `sem_destroy` | BX = handle | — | Error while threads are waiting on it |
| 12h | `mbox_destroy` | BX = handle | — | Error while threads are waiting on it |
| 18h | `open` | BX = file name pointer | AX = file handle | Root directory of the boot disk, read-only; at most 4 files open system-wide |
| 19h | `read` | BX = handle, CX = length, DX = buffer pointer | AX = bytes read | 0 at end of file; may stop the scheduler briefly (see docs/programs.md) |
| 1Ah | `close` | BX = handle | — | Files are also closed when their thread ends |
| 1Bh | `exec` | BX = program file name pointer | AX = thread ID | Loads and starts a program; see docs/programs.md |

## Argument checks

When the caller is a loaded program, the kernel checks its arguments before using them. A call that fails a check returns the usual error (carry set, AX = FFFFh) and is reported in telemetry as a `THREAD_FAULT` record of kind `bad_argument`, naming the call.

| Argument | Accepted only if |
| --- | --- |
| A pointer (`puts`, `open`, `read`, `exec`) | DS is the program's own data segment, and the whole buffer, or the string up to its terminator, lies between offset 0010h and the end of the program's stacks. A string is not read past that end |
| A thread entry point (`thread_create`) | The call comes from the program's own code segment and the entry lies inside its code |
| A semaphore or mailbox handle | The kernel handed it out from `sem_create` or `mbox_create`, for that kind of object, and it has not been destroyed. This applies to kernel threads too |
| A segment to `free` | It starts a far block that the calling thread, or another thread of the same program, allocated. The kernel's own blocks, such as a program's code and data, are refused |

Kernel threads are trusted with pointers and entry points. None of this is memory protection: the 8086 lets a program write anywhere without asking the kernel. The checks only make sure the kernel does not do damage on a program's behalf. `sdk/examples/rogue.c` tries each kind of bad argument.

## Known gaps

* Far memory belongs to the thread that allocated it and is freed when that thread ends, even if another thread of the same program is still using it.
* `mbox_recv` cannot report an error in AX, because every 16-bit value is a valid message; check the carry flag.
* At most 16 semaphores and mailboxes made through system calls can exist at once. A program's are destroyed when its last thread ends; a kernel thread's last until it destroys them.

## Example

The `syscall` shell command runs a thread that uses only `INT 80h` (`demo_syscall` in `kernel/shell.c`). The `selftest` command exercises calls 00h, 09h–12h and 18h–1Bh. The programs in `sdk/examples/` use the calls through `sdk/include/phoenix.h`.
