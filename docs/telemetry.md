# Phoenix-8086 Telemetry Protocol

Protocol version 1 (not frozen until v0.9). Kernel side: `kernel/telemetry.c`. Host side: `bridge/protocol.py`.

## Design

* The kernel **records** events into a 4 KB ring buffer. That is all an interrupt handler ever does.
* A kernel thread (`telemetry`, priority 12) wakes ten times a second, frames the buffered records, and sends them over COM1 (115200 8N1). It also sends the periodic records.
* If the ring is full, a record is **dropped and counted**. The count is reported in `COUNTERS`. The kernel never invents data.
* High-volume records (`CONTEXT_SWITCH`, `THREAD_STATE`, `SYSCALL`) may fill only three quarters of the ring, so a flood of them cannot crowd out console text or a fault report.
* Console output is a record type (`CONSOLE`), not raw text mixed into the stream.
* On a panic the buffer is flushed synchronously before the machine halts.
* Building with `make TELEMETRY=0` compiles all of this out; the console is then mirrored to the serial port as plain text.

The telemetry thread is an ordinary thread, so its own sleeps and context switches appear in the stream like any other thread's.

## Frame

```
7E  version  type  seq  tick[4]  payload...  crc[2]  7E
```

| Field | Size | Meaning |
| --- | --- | --- |
| `7E` | 1 | Frame delimiter, before and after every frame |
| version | 1 | Protocol version (1) |
| type | 1 | Record type |
| seq | 1 | Increments by one per frame sent, wraps at 256; a gap means frames were lost on the wire. A `BOOT_STAGE` record for stage 1 marks a (re)boot and restarts the numbering |
| tick | 4 | Kernel tick when the event happened (100 ticks per second) |
| payload | 0–64 | Depends on the type |
| crc | 2 | CRC-16/CCITT-FALSE (polynomial 1021h, initial value FFFFh) over version through payload |

All multi-byte values are little-endian. Between the delimiters, a byte equal to `7E` or `7D` is sent as `7D` followed by the byte XOR `20`.

A receiver discards any frame with a bad CRC, an unknown version, or a payload too short for its type.

## Record types

| Type | Name | Payload | Sent |
| --- | --- | --- | --- |
| 00h | `HELLO` | version u8, hz u8, max_threads u8, reserved u8, code_seg u16, data_seg u16, far_start_seg u16, far_end_seg u16, mem_kb u16, heap_start u16, heap_end u16 | Every second |
| 01h | `BOOT_STAGE` | stage u8 | During boot (stages 1–7) |
| 02h | `THREAD_CREATE` | tid u8, priority u8, name[12] | On creation |
| 03h | `THREAD_EXIT` | tid u8 | On exit or kill |
| 04h | `THREAD_STATE` | tid u8, state u8, priority u8 | When a thread blocks, sleeps, wakes, or changes priority |
| 05h | `CONTEXT_SWITCH` | from_tid u8, to_tid u8, then IP, CS, FLAGS, SP, AX, BX, CX, DX, SI, DI, BP (u16 each), sub_tick u16 | On every switch; the registers are the ones the incoming thread resumes with |
| 06h | `COUNTERS` | timer u32, keyboard u32, syscall u32, context_switches u32, drops u16 | Five times a second |
| 07h | `MEMORY` | heap_free u16, heap_used u16, far_free_paras u16, far_total_paras u16 | Every second |
| 08h | `FAULT` | tid u8, registers as in `CONTEXT_SWITCH`, reason text | On panic or CPU exception |
| 09h | `CONSOLE` | text, up to 48 bytes | On newline, when 48 characters are waiting, or at the next wake-up |
| 0Ah | `SYSCALL` | tid u8, function u8 | On every `INT 80h` |
| 0Bh | `BENCH` | kind u8 (0 = context switches per second, 1 = heap pairs per second, 2 = average timer interrupt latency in ns, 3 = longest latency in ns), value u32 | By the `bench` command |
| 0Ch | `THREAD_STATS` | tid u8, state u8, priority u8, cpu_ticks u32, saved_sp u16, stack_base u16, stack_size u16, name[12], stack_peak u16, program_stack_peak u16 | Every second, one per thread |
| 0Dh | `THREAD_FAULT` | tid u8, kind u8, detail u16 | When a thread overflows a stack (and is stopped) or a system call argument is refused |
| 0Eh | `PRIORITY` | tid u8, effective u8, reason u8, cause u8 | When a thread inherits a priority through a mutex, and when it gives it back |

`CONTEXT_SWITCH`: `sub_tick` is newer than the other fields, and a receiver must accept the 24-byte form without it. It is how far into the tick the switch happened, in timer-chip counts (1,193,182 per second, about 0.84 µs each), so `tick × 11932 + sub_tick` orders switches within a tick and measures how long each thread ran. It can exceed 11931 when the next tick's interrupt was already due; it never runs backwards.

`THREAD_STATS`: the last two fields are newer than the rest, and a receiver must accept the 25-byte form without them. `stack_peak` is the deepest the thread's kernel stack has ever been used, in bytes, measured from the fill pattern written when the thread was created; `program_stack_peak` is the same for a program thread's own stack and 0 for a kernel thread.

`THREAD_FAULT` kinds: 0 kernel stack overflow, 1 program stack overflow (for both, `detail` is the stack pointer at the check), 2 refused system call argument (`detail` is the function number; see [Argument checks](syscalls.md#argument-checks)). Unlike `FAULT`, the kernel carries on: the dashboard lists these as events, not as a panic.

`PRIORITY` reasons: 1 inherit (`cause` is the thread waiting on a mutex this one holds, `effective` the priority it now runs at), 2 restore (back to its own priority after releasing its last mutex; `cause` is FFh). Aging changes effective priority too often to report.

Thread states: 0 READY, 1 RUNNING, 2 BLOCKED, 3 SLEEPING, 4 TERMINATED. Running and ready transitions are implied by `CONTEXT_SWITCH` and are not sent as `THREAD_STATE`.

Boot stages: 1 kernel entry, 2 console, 3 memory manager, 4 scheduler, 5 interrupts, 6 shell thread created, 7 boot complete.

`HELLO`, `MEMORY`, and `THREAD_STATS` repeat every second so that a receiver that connects late can rebuild the full picture within a second.

## Input

Bytes received on COM1 are treated as typed characters and placed in the keyboard buffer (CR becomes Enter, DEL becomes Backspace). They are polled by the telemetry thread, so input can lag by up to 100 ms. Input is unframed.

## Bridge and captures

`bridge/serial_ws_bridge.py` decodes the stream and relays each record as a JSON object over WebSocket, with the field names above plus `type`, `seq`, and `tick`. It adds one message of its own, `BRIDGE`, describing the link (mode, frames, damaged frames, lost frames).

```sh
python3 bridge/serial_ws_bridge.py --capture session.jsonl     # live, recording
python3 bridge/serial_ws_bridge.py --replay session.jsonl --speed 2
```

A capture is a JSON Lines file: a header line, then one `{"t": seconds, "msg": {...}}` line per record.

## Opening a capture in Perfetto

`bridge/trace.py` converts a capture to the Chrome JSON trace format, which [Perfetto](https://ui.perfetto.dev) and `chrome://tracing` open directly:

```sh
python3 -m bridge.trace session.jsonl -o session.json
```

The trace has a CPU track with one slice for each stretch a thread held the processor, a track per thread (with its system calls, state changes, priority loans and faults as markers), a flow arrow at every context switch, counters for switch rate, dropped records and memory, and boot stages, panics and console lines. Context switches are placed to within a microsecond using their `sub_tick`; other events, and switches from captures made before `sub_tick` existed, at the start of their tick. Nothing is given a finer time than the kernel reported.
