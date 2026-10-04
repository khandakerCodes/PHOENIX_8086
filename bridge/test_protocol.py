# SPDX-License-Identifier: MIT
"""Unit tests for the telemetry protocol decoder. Run: python3 -m unittest discover bridge"""

import struct
import unittest

from bridge import protocol
from bridge.protocol import TYPE_IDS, Decoder, encode_frame


class FramingTest(unittest.TestCase):
    def test_round_trip(self):
        wire = encode_frame(TYPE_IDS["BOOT_STAGE"], 5, 1234, bytes([3]))
        (message,) = Decoder().feed(wire)
        self.assertEqual(message, {"type": "BOOT_STAGE", "seq": 5, "tick": 1234, "stage": 3})

    def test_delimiter_and_escape_bytes_in_payload(self):
        text = bytes([0x7E, 0x7D, 0x41, 0x7E])
        wire = encode_frame(TYPE_IDS["CONSOLE"], 0, 0, text)
        self.assertEqual(wire.count(0x7E), 2)      # only the two real delimiters
        (message,) = Decoder().feed(wire)
        self.assertEqual(message["text"].encode("latin-1"), text)

    def test_split_across_reads(self):
        wire = encode_frame(TYPE_IDS["THREAD_EXIT"], 0, 7, bytes([2]))
        decoder = Decoder()
        messages = []
        for byte in wire:
            messages += decoder.feed(bytes([byte]))
        self.assertEqual([m["tid"] for m in messages], [2])

    def test_corrupted_frame_is_dropped_and_stream_recovers(self):
        good = encode_frame(TYPE_IDS["THREAD_EXIT"], 1, 0, bytes([4]))
        bad = bytearray(encode_frame(TYPE_IDS["THREAD_EXIT"], 0, 0, bytes([3])))
        bad[3] ^= 0x01
        decoder = Decoder()
        messages = decoder.feed(bytes(bad) + good)
        self.assertEqual([m["tid"] for m in messages], [4])
        self.assertEqual(decoder.bad_frames, 1)

    def test_truncated_frame_is_dropped(self):
        whole = encode_frame(TYPE_IDS["THREAD_EXIT"], 0, 0, bytes([1]))
        decoder = Decoder()
        messages = decoder.feed(whole[:5] + whole)
        self.assertEqual(len(messages), 1)
        self.assertEqual(decoder.bad_frames, 1)

    def test_sequence_gap_is_counted(self):
        decoder = Decoder()
        decoder.feed(encode_frame(TYPE_IDS["BOOT_STAGE"], 254, 0, b"\x01"))
        decoder.feed(encode_frame(TYPE_IDS["BOOT_STAGE"], 255, 0, b"\x02"))
        decoder.feed(encode_frame(TYPE_IDS["BOOT_STAGE"], 0, 0, b"\x03"))    # wraps, no loss
        self.assertEqual(decoder.lost_frames, 0)
        decoder.feed(encode_frame(TYPE_IDS["BOOT_STAGE"], 3, 0, b"\x04"))    # 1 and 2 missing
        self.assertEqual(decoder.lost_frames, 2)

    def test_a_reboot_restarts_the_numbering_without_counting_a_loss(self):
        decoder = Decoder()
        decoder.feed(encode_frame(TYPE_IDS["CONSOLE"], 200, 900, b"bye"))
        decoder.feed(encode_frame(TYPE_IDS["BOOT_STAGE"], 0, 0, b"\x01"))
        decoder.feed(encode_frame(TYPE_IDS["BOOT_STAGE"], 1, 0, b"\x02"))
        self.assertEqual(decoder.lost_frames, 0)
        decoder.feed(encode_frame(TYPE_IDS["BOOT_STAGE"], 5, 0, b"\x03"))
        self.assertEqual(decoder.lost_frames, 3)

    def test_bytes_outside_frames_are_counted_not_decoded(self):
        decoder = Decoder()
        self.assertEqual(decoder.feed(b"boot noise"), [])
        self.assertEqual(decoder.stray_bytes, 10)

    def test_wrong_version_is_rejected(self):
        content = struct.pack("<BBBI", 9, TYPE_IDS["BOOT_STAGE"], 0, 0) + b"\x01"
        body = content + struct.pack("<H", protocol.crc16(content))
        decoder = Decoder()
        self.assertEqual(decoder.feed(bytes([0x7E]) + body + bytes([0x7E])), [])
        self.assertEqual(decoder.bad_frames, 1)

    def test_crc_matches_reference_value(self):
        # CRC-16/CCITT-FALSE check value
        self.assertEqual(protocol.crc16(b"123456789"), 0x29B1)


class PayloadTest(unittest.TestCase):
    def decode(self, name, payload):
        (message,) = Decoder().feed(encode_frame(TYPE_IDS[name], 0, 0, payload))
        return message

    def test_thread_create(self):
        message = self.decode("THREAD_CREATE", bytes([2, 10]) + b"shell".ljust(12, b"\0"))
        self.assertEqual((message["tid"], message["priority"], message["name"]), (2, 10, "shell"))

    def test_thread_state(self):
        message = self.decode("THREAD_STATE", bytes([1, 3, 6]))
        self.assertEqual((message["state"], message["priority"]), ("SLEEPING", 6))

    def test_context_switch_registers(self):
        regs = range(0x1000, 0x1000 + 11)
        message = self.decode("CONTEXT_SWITCH", bytes([0, 1]) + struct.pack("<11H", *regs))
        self.assertEqual((message["from_tid"], message["to_tid"]), (0, 1))
        self.assertEqual(message["regs"]["ip"], 0x1000)
        self.assertEqual(message["regs"]["bp"], 0x100A)

    def test_counters(self):
        message = self.decode("COUNTERS", struct.pack("<4IH", 70000, 2, 3, 4, 5))
        self.assertEqual(message["timer"], 70000)
        self.assertEqual(message["drops"], 5)

    def test_fault(self):
        payload = bytes([1]) + struct.pack("<11H", *range(11)) + b"Divide error"
        message = self.decode("FAULT", payload)
        self.assertEqual(message["reason"], "Divide error")
        self.assertEqual(message["regs"]["cs"], 1)

    def test_thread_stats(self):
        payload = struct.pack("<3BI3H", 1, 1, 10, 123456, 0x8000, 0x7800, 2048) + b"shell".ljust(12, b"\0")
        message = self.decode("THREAD_STATS", payload)
        self.assertEqual((message["state"], message["cpu_ticks"], message["name"]),
                         ("RUNNING", 123456, "shell"))

    def test_thread_stats_with_stack_peaks(self):
        payload = (struct.pack("<3BI3H", 3, 0, 5, 7, 0x8000, 0x7800, 2048) +
                   b"rogue".ljust(12, b"\0") + struct.pack("<2H", 312, 1980))
        message = self.decode("THREAD_STATS", payload)
        self.assertEqual((message["stack_peak"], message["program_stack_peak"]), (312, 1980))

    def test_thread_stats_from_an_older_kernel_has_no_peaks(self):
        payload = struct.pack("<3BI3H", 1, 1, 10, 1, 0, 0, 2048) + b"shell".ljust(12, b"\0")
        self.assertNotIn("stack_peak", self.decode("THREAD_STATS", payload))

    def test_thread_fault(self):
        message = self.decode("THREAD_FAULT", struct.pack("<BBH", 4, 1, 0x0812))
        self.assertEqual((message["tid"], message["kind"], message["detail"]),
                         (4, "program_stack", 0x0812))
        message = self.decode("THREAD_FAULT", struct.pack("<BBH", 4, 2, 0x0B))
        self.assertEqual((message["kind"], message["detail"]), ("bad_argument", 0x0B))

    def test_priority(self):
        message = self.decode("PRIORITY", bytes([3, 8, 1, 5]))
        self.assertEqual((message["tid"], message["effective"], message["reason"], message["cause"]),
                         (3, 8, "inherit", 5))
        message = self.decode("PRIORITY", bytes([3, 2, 2, 0xFF]))
        self.assertEqual((message["reason"], message["cause"]), ("restore", None))

    def test_short_payload_is_rejected(self):
        decoder = Decoder()
        self.assertEqual(decoder.feed(encode_frame(TYPE_IDS["COUNTERS"], 0, 0, b"\x01\x02")), [])
        self.assertEqual(decoder.bad_frames, 1)

    def test_unknown_type_is_passed_through(self):
        (message,) = Decoder().feed(encode_frame(0x7A, 0, 0, b"\x01\x02"))
        self.assertEqual((message["type"], message["data"]), ("UNKNOWN_7A", [1, 2]))


if __name__ == "__main__":
    unittest.main()
