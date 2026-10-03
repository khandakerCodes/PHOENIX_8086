# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Telemetry protocol v1 (host side)

Decodes the framed serial stream produced by kernel/telemetry.c into
dictionaries, and can encode frames for tests. See docs/telemetry.md.

Frame on the wire:

    7E  version type seq tick[4] payload... crc[2]  7E

Bytes 7E and 7D between the delimiters are sent as 7D, byte^20.
The CRC is CRC-16/CCITT-FALSE over version..payload, low byte first.
"""

import binascii
import struct

PROTOCOL_VERSION = 1

DELIMITER = 0x7E
ESCAPE = 0x7D
ESCAPE_XOR = 0x20

HEADER_SIZE = 7     # version, type, seq, tick[4]
CRC_SIZE = 2

TYPE_NAMES = {
    0x00: "HELLO",
    0x01: "BOOT_STAGE",
    0x02: "THREAD_CREATE",
    0x03: "THREAD_EXIT",
    0x04: "THREAD_STATE",
    0x05: "CONTEXT_SWITCH",
    0x06: "COUNTERS",
    0x07: "MEMORY",
    0x08: "FAULT",
    0x09: "CONSOLE",
    0x0A: "SYSCALL",
    0x0B: "BENCH",
    0x0C: "THREAD_STATS",
}
TYPE_IDS = {name: number for number, name in TYPE_NAMES.items()}

THREAD_STATES = ["READY", "RUNNING", "BLOCKED", "SLEEPING", "TERMINATED"]
BENCH_KINDS = ["context_switches", "heap_pairs"]

REGISTERS = ("ip", "cs", "flags", "sp", "ax", "bx", "cx", "dx", "si", "di", "bp")


def crc16(data):
    """CRC-16/CCITT-FALSE."""
    return binascii.crc_hqx(bytes(data), 0xFFFF)


def _name(raw):
    return raw.split(b"\0", 1)[0].decode("ascii", errors="replace")


def _state(value):
    return THREAD_STATES[value] if value < len(THREAD_STATES) else f"UNKNOWN_{value}"


def _registers(payload, offset):
    values = struct.unpack_from("<11H", payload, offset)
    return dict(zip(REGISTERS, values))


def decode_payload(type_name, p):
    """Turn a record payload into named fields. Raises on a short payload."""
    if type_name == "HELLO":
        version, hz, max_threads, _, code, data, far_start, far_end, mem_kb, heap_start, heap_end = \
            struct.unpack_from("<4B7H", p)
        return {"version": version, "hz": hz, "max_threads": max_threads,
                "code_seg": code, "data_seg": data,
                "far_start_seg": far_start, "far_end_seg": far_end,
                "mem_kb": mem_kb, "heap_start": heap_start, "heap_end": heap_end}
    if type_name == "BOOT_STAGE":
        return {"stage": p[0]}
    if type_name == "THREAD_CREATE":
        return {"tid": p[0], "priority": p[1], "name": _name(p[2:14])}
    if type_name == "THREAD_EXIT":
        return {"tid": p[0]}
    if type_name == "THREAD_STATE":
        return {"tid": p[0], "state": _state(p[1]), "priority": p[2]}
    if type_name == "CONTEXT_SWITCH":
        return {"from_tid": p[0], "to_tid": p[1], "regs": _registers(p, 2)}
    if type_name == "COUNTERS":
        timer, keyboard, syscall, switches, drops = struct.unpack_from("<4IH", p)
        return {"timer": timer, "keyboard": keyboard, "syscall": syscall,
                "context_switches": switches, "drops": drops}
    if type_name == "MEMORY":
        heap_free, heap_used, far_free, far_total = struct.unpack_from("<4H", p)
        return {"heap_free": heap_free, "heap_used": heap_used,
                "far_free_paras": far_free, "far_total_paras": far_total}
    if type_name == "FAULT":
        return {"tid": p[0], "regs": _registers(p, 1),
                "reason": p[23:].decode("ascii", errors="replace")}
    if type_name == "CONSOLE":
        return {"text": p.decode("ascii", errors="replace")}
    if type_name == "SYSCALL":
        return {"tid": p[0], "func": p[1]}
    if type_name == "BENCH":
        kind, count = struct.unpack_from("<BI", p)
        return {"kind": BENCH_KINDS[kind] if kind < len(BENCH_KINDS) else f"UNKNOWN_{kind}",
                "count": count}
    if type_name == "THREAD_STATS":
        tid, state, priority, cpu_ticks, sp, stack_base, stack_size = struct.unpack_from("<3BI3H", p)
        return {"tid": tid, "state": _state(state), "priority": priority,
                "cpu_ticks": cpu_ticks, "sp": sp, "stack_base": stack_base,
                "stack_size": stack_size, "name": _name(p[13:25])}
    return {"data": list(p)}


def decode_frame(body):
    """
    Decode one unescaped frame body (everything between two delimiters).
    Returns a message dict, or None if the frame is damaged.
    """
    if len(body) < HEADER_SIZE + CRC_SIZE:
        return None
    content, (crc,) = body[:-CRC_SIZE], struct.unpack("<H", body[-CRC_SIZE:])
    if crc16(content) != crc:
        return None

    version, type_id, seq, tick = struct.unpack_from("<BBBI", content)
    if version != PROTOCOL_VERSION:
        return None

    type_name = TYPE_NAMES.get(type_id, f"UNKNOWN_{type_id:02X}")
    message = {"type": type_name, "seq": seq, "tick": tick}
    try:
        message.update(decode_payload(type_name, bytes(content[HEADER_SIZE:])))
    except (struct.error, IndexError):
        return None
    return message


def encode_frame(type_id, seq, tick, payload=b""):
    """Build the wire bytes for one frame (used by tests)."""
    content = struct.pack("<BBBI", PROTOCOL_VERSION, type_id, seq & 0xFF, tick) + bytes(payload)
    body = content + struct.pack("<H", crc16(content))

    wire = bytearray([DELIMITER])
    for byte in body:
        if byte in (DELIMITER, ESCAPE):
            wire += bytes([ESCAPE, byte ^ ESCAPE_XOR])
        else:
            wire.append(byte)
    wire.append(DELIMITER)
    return bytes(wire)


class Decoder:
    """
    Incremental stream decoder. Feed it bytes as they arrive; it returns
    the complete messages and keeps count of what it had to throw away.
    """

    def __init__(self):
        self._body = bytearray()
        self._escaped = False
        self._in_frame = False
        self._last_seq = None
        self.frames = 0         # Good frames decoded
        self.bad_frames = 0     # Frames discarded (CRC, length, version)
        self.lost_frames = 0    # Frames missing according to sequence numbers
        self.stray_bytes = 0    # Bytes seen outside any frame

    def feed(self, data):
        messages = []
        for byte in data:
            if byte == DELIMITER:
                if self._in_frame and self._body:
                    self._finish(messages)
                # A delimiter both ends one frame and may start the next
                self._in_frame = True
                self._body.clear()
                self._escaped = False
            elif not self._in_frame:
                self.stray_bytes += 1
            elif byte == ESCAPE:
                self._escaped = True
            else:
                self._body.append(byte ^ ESCAPE_XOR if self._escaped else byte)
                self._escaped = False
        return messages

    def _finish(self, messages):
        message = decode_frame(bytes(self._body))
        if message is None:
            self.bad_frames += 1
            return
        if self._last_seq is not None:
            self.lost_frames += (message["seq"] - self._last_seq - 1) & 0xFF
        self._last_seq = message["seq"]
        self.frames += 1
        messages.append(message)


def console_text(messages):
    """Join the text of all CONSOLE messages."""
    return "".join(m["text"] for m in messages if m["type"] == "CONSOLE")
