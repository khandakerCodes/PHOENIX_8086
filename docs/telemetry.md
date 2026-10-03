# Phoenix-8086 Telemetry Protocol

Protocol version 1 (not frozen until v0.9). Kernel side: `kernel/telemetry.c`. Host side: `bridge/protocol.py`.

## Design

* The kernel **records** events into a 4 KB ring buffer. That is all an interrupt handler ever does.
* A kernel thread (`telemetry`, priority 12) wakes ten times a second, frames the buffered records, and sends them over COM1 (115200 8N1). It also sends the periodic records.
* If the ring is full, a record is **dropped and counted**. The count is reported in `COUNTERS`. The kernel never invents data.
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
| seq | 1 | Increments by one per frame sent, wraps at 256; a gap means frames were lost on the wire |
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
| 05h | `CONTEXT_SWITCH` | from_tid u8, to_tid u8, then IP, CS, FLAGS, SP, AX, BX, CX, DX, SI, DI, BP (u16 each) | On every switch; the registers are the ones the incoming thread resumes with |
| 06h | `COUNTERS` | timer u32, keyboard u32, syscall u32, context_switches u32, drops u16 | Five times a second |
| 07h | `MEMORY` | heap_free u16, heap_used u16, far_free_paras u16, far_total_paras u16 | Every second |
| 08h | `FAULT` | tid u8, registers as in `CONTEXT_SWITCH`, reason text | On panic or CPU exception |
| 09h | `CONSOLE` | text, up to 48 bytes | On newline, when 48 characters are waiting, or at the next wake-up |
| 0Ah | `SYSCALL` | tid u8, function u8 | On every `INT 80h` |
| 0Bh | `BENCH` | kind u8 (0 = context switches, 1 = heap pairs), count per second u32 | By the `bench` command |
| 0Ch | `THREAD_STATS` | tid u8, state u8, priority u8, cpu_ticks u32, saved_sp u16, stack_base u16, stack_size u16, name[12] | Every second, one per thread |

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
