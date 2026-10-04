# SPDX-License-Identifier: MIT
"""Tests for the capture → Chrome/Perfetto trace converter (bridge/trace.py)."""

import json
import os
import subprocess
import sys
import tempfile
import unittest

from bridge.capture import read_capture
from bridge.trace import CPU_TID, capture_to_trace

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SESSION = os.path.join(ROOT, "dashboard", "test", "fixtures", "session.jsonl")


def switch(tick, from_tid, to_tid):
    return {"type": "CONTEXT_SWITCH", "tick": tick, "from_tid": from_tid, "to_tid": to_tid, "regs": {}}


class SessionTraceTest(unittest.TestCase):
    """A real recorded session: boot, demo threads, IPC, system calls, panic."""

    @classmethod
    def setUpClass(cls):
        cls.entries = read_capture(SESSION)
        cls.trace = capture_to_trace(cls.entries)
        cls.events = cls.trace["traceEvents"]

    def test_is_valid_json_with_known_phases(self):
        json.loads(json.dumps(self.trace))
        self.assertTrue({e["ph"] for e in self.events} <= {"M", "X", "i", "s", "f", "C"})

    def test_one_cpu_slice_per_context_switch(self):
        switches = sum(1 for _, m in self.entries if m["type"] == "CONTEXT_SWITCH")
        cpu = [e for e in self.events if e["ph"] == "X" and e["tid"] == CPU_TID]
        self.assertEqual(len(cpu), switches)

    def test_cpu_slices_never_overlap_or_run_backwards(self):
        cpu = [e for e in self.events if e["ph"] == "X" and e["tid"] == CPU_TID]
        for before, after in zip(cpu, cpu[1:]):
            self.assertGreaterEqual(before["dur"], 0)
            self.assertLessEqual(before["ts"] + before["dur"], after["ts"])

    def test_every_flow_has_a_start_and_an_end(self):
        starts = {e["id"] for e in self.events if e["ph"] == "s"}
        ends = {e["id"] for e in self.events if e["ph"] == "f"}
        self.assertTrue(starts)
        self.assertEqual(starts, ends)

    def test_threads_are_named_and_demo_threads_ran(self):
        names = {e["args"]["name"] for e in self.events if e["ph"] == "M" and e["name"] == "thread_name"}
        self.assertIn("CPU", names)
        self.assertTrue(any(n.startswith("shell (TID") for n in names), names)
        ran = {e["name"] for e in self.events if e["ph"] == "X"}
        self.assertTrue({"demo-A", "demo-B", "demo-C"} <= ran, ran)

    def test_panic_is_a_global_marker(self):
        panics = [e for e in self.events if e["ph"] == "i" and e["name"].startswith("KERNEL PANIC")]
        self.assertEqual(len(panics), 1)
        self.assertEqual(panics[0]["s"], "g")

    def test_counters_are_present(self):
        names = {e["name"] for e in self.events if e["ph"] == "C"}
        self.assertTrue({"context switches per second", "near heap (bytes)"} <= names, names)


class TrackTest(unittest.TestCase):
    def test_a_reused_tid_gets_a_new_track(self):
        messages = [
            {"type": "THREAD_CREATE", "tick": 1, "tid": 3, "priority": 5, "name": "hello"},
            switch(2, 0, 3),
            {"type": "THREAD_EXIT", "tick": 3, "tid": 3},
            switch(3, 3, 0),
            {"type": "THREAD_CREATE", "tick": 4, "tid": 3, "priority": 5, "name": "primes"},
            switch(5, 0, 3),
        ]
        trace = capture_to_trace([(0.0, m) for m in messages])
        named = {e["args"]["name"]: e["tid"] for e in trace["traceEvents"]
                 if e["ph"] == "M" and e["name"] == "thread_name"}
        self.assertNotEqual(named["hello (TID 3)"], named["primes (TID 3)"])

    def test_times_come_from_ticks_only(self):
        trace = capture_to_trace([(0.0, switch(10, 0, 1)), (0.0, switch(12, 1, 0))])
        (slice_,) = [e for e in trace["traceEvents"] if e["ph"] == "X" and e["tid"] == CPU_TID][:1]
        self.assertEqual((slice_["ts"], slice_["dur"]), (100_000, 20_000))   # 100 Hz: 10 ms a tick


class CommandLineTest(unittest.TestCase):
    def test_writes_a_trace_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "session.json")
            subprocess.run([sys.executable, "-m", "bridge.trace", SESSION, "-o", out],
                           cwd=ROOT, check=True, capture_output=True)
            with open(out, encoding="utf-8") as f:
                self.assertIn("traceEvents", json.load(f))


if __name__ == "__main__":
    unittest.main()
