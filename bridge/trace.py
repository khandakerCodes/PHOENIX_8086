# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Export a telemetry capture as a Chrome / Perfetto trace

Turns a capture file (bridge/capture.py) into the Chrome JSON trace
format, which https://ui.perfetto.dev and chrome://tracing open
directly:

    python3 -m bridge.trace session.jsonl -o session.json

What the trace shows:

  * A "CPU" track: one slice for every stretch a thread had the
    processor, named after the thread.
  * One track per thread, with its running slices and instant markers
    for state changes, system calls and thread faults. A thread ID the
    kernel reuses gets a new track, so each track is one thread's life.
  * A flow arrow at every context switch, from the thread that gave up
    the processor to the one that got it.
  * Counters: context switches per second, telemetry records dropped,
    near-heap use and free far memory.
  * Global markers for boot stages and panics; console lines on a
    track of their own.

Times come from the kernel's tick counter (10 ms at 100 Hz). Context
switches from newer kernels also carry the timer chip's count within
the tick (about 0.84 us resolution), so the CPU and thread slices
have real durations. Other events, and switches from older captures,
are placed at the start of their tick; nothing here invents a finer
time than the kernel reported.
"""

import argparse
import json
import sys

from bridge.capture import read_capture
from bridge.protocol import PIT_HZ

DEFAULT_HZ = 100
KERNEL_PID = 1              # one "process" for the kernel's threads
CPU_TID = 0                 # its CPU track
CONSOLE_TID = 1             # its console track
FIRST_THREAD_TRACK = 10     # thread tracks are numbered from here

SYSCALL_NAMES = {
    0x00: "version", 0x01: "putc", 0x02: "puts", 0x03: "getc",
    0x04: "thread_create", 0x05: "thread_exit", 0x06: "yield", 0x07: "sleep",
    0x08: "ticks", 0x09: "sem_create", 0x0A: "sem_wait", 0x0B: "sem_signal",
    0x0C: "mbox_create", 0x0D: "mbox_send", 0x0E: "mbox_recv",
    0x0F: "alloc", 0x10: "free", 0x11: "sem_destroy", 0x12: "mbox_destroy",
    0x18: "open", 0x19: "read", 0x1A: "close", 0x1B: "exec",
}


class TraceBuilder:
    """Feed decoded telemetry messages in order; collect trace events."""

    def __init__(self):
        self.events = []
        self.hz = DEFAULT_HZ
        self._tracks = {}           # kernel TID → track id of its current life
        self._names = {}            # track id → thread name
        self._next_track = FIRST_THREAD_TRACK
        self._running = None        # (kernel TID, track id, start µs) or None
        self._flow_id = 0
        self._last = None           # previous COUNTERS record
        self._end = 0

    # ── Helpers ──

    def _us(self, tick, sub_tick=0):
        # sub_tick: PIT counts into the tick, from CONTEXT_SWITCH records of newer kernels
        return tick * 1_000_000 // self.hz + sub_tick * 1_000_000 // PIT_HZ

    def _emit(self, **event):
        event.setdefault("pid", KERNEL_PID)
        self.events.append(event)

    def _track(self, tid, name=None):
        """The track for a kernel TID, starting one if it has none yet."""
        if tid not in self._tracks:
            self._new_track(tid, name or f"T{tid}")
        return self._tracks[tid]

    def _new_track(self, tid, name):
        track = self._next_track
        self._next_track += 1
        self._tracks[tid] = track
        self._names[track] = name
        self._emit(ph="M", name="thread_name", tid=track, args={"name": f"{name} (TID {tid})"})
        self._emit(ph="M", name="thread_sort_index", tid=track, args={"sort_index": track})
        return track

    def _instant(self, tick, track, name, scope="t", args=None):
        event = {"ph": "i", "name": name, "ts": self._us(tick), "tid": track, "s": scope}
        if args:
            event["args"] = args
        self._emit(**event)

    def _close_running(self, ts):
        if self._running is None:
            return None
        tid, track, start = self._running
        name = self._names.get(track, f"T{tid}")
        for on in (track, CPU_TID):
            self._emit(ph="X", name=name, cat="run", ts=start, dur=ts - start, tid=on,
                       args={"tid": tid})
        self._running = None
        return track

    # ── Messages ──

    def add(self, message):
        tick = message.get("tick", 0)
        self._end = max(self._end, self._us(tick, message.get("sub_tick", 0)))
        handler = getattr(self, "_on_" + message.get("type", "").lower(), None)
        if handler:
            handler(message, tick)

    def _on_hello(self, m, tick):
        if m.get("hz"):
            self.hz = m["hz"]

    def _on_boot_stage(self, m, tick):
        self._instant(tick, CPU_TID, f"Boot stage {m['stage']}", scope="g")

    def _on_thread_create(self, m, tick):
        track = self._new_track(m["tid"], m["name"])
        self._instant(tick, track, "created", args={"priority": m["priority"]})

    def _on_thread_exit(self, m, tick):
        track = self._track(m["tid"])
        if self._running and self._running[1] == track:
            self._close_running(self._us(tick))
        self._instant(tick, track, "exited")
        del self._tracks[m["tid"]]

    def _on_thread_state(self, m, tick):
        self._instant(tick, self._track(m["tid"]), m["state"], args={"priority": m["priority"]})

    def _on_context_switch(self, m, tick):
        ts = self._us(tick, m.get("sub_tick", 0))
        if self._running is not None:
            ts = max(ts, self._running[2])     # a slice never ends before it started
        previous = self._close_running(ts)
        if previous is None and m["from_tid"] in self._tracks:
            previous = self._tracks[m["from_tid"]]
        track = self._track(m["to_tid"])
        if previous is not None:
            # A flow arrow from the outgoing thread to the incoming one
            self._flow_id += 1
            self._emit(ph="s", name="switch", cat="switch", id=self._flow_id, ts=ts, tid=previous)
            self._emit(ph="f", name="switch", cat="switch", id=self._flow_id, ts=ts, tid=track,
                       bp="e")
        self._running = (m["to_tid"], track, ts)

    def _on_syscall(self, m, tick):
        name = SYSCALL_NAMES.get(m["func"], f"0x{m['func']:02X}")
        self._instant(tick, self._track(m["tid"]), f"INT 80h {name}")

    def _on_priority(self, m, tick):
        if m["reason"] == "inherit":
            label = f"priority {m['effective']}, lent by TID {m['cause']}"
        else:
            label = f"priority back to {m['effective']}"
        self._instant(tick, self._track(m["tid"]), label)

    def _on_thread_fault(self, m, tick):
        self._instant(tick, self._track(m["tid"]), f"thread fault: {m['kind']}",
                      args={"detail": m["detail"]})

    def _on_fault(self, m, tick):
        self._instant(tick, CPU_TID, f"KERNEL PANIC: {m['reason']}", scope="g",
                      args={"tid": m["tid"], **m["regs"]})

    def _on_console(self, m, tick):
        text = m["text"].rstrip("\n")
        if text.strip():
            self._instant(tick, CONSOLE_TID, text[:80])

    def _on_counters(self, m, tick):
        last, self._last = self._last, m
        ts = self._us(tick)
        self._emit(ph="C", name="telemetry records dropped", ts=ts, tid=CPU_TID,
                   args={"dropped": m["drops"]})
        if last is None or tick <= last["tick"] or m["context_switches"] < last["context_switches"]:
            return      # first record, or the machine rebooted
        rate = (m["context_switches"] - last["context_switches"]) * self.hz / (tick - last["tick"])
        self._emit(ph="C", name="context switches per second", ts=ts, tid=CPU_TID,
                   args={"switches": round(rate)})

    def _on_memory(self, m, tick):
        ts = self._us(tick)
        self._emit(ph="C", name="near heap (bytes)", ts=ts, tid=CPU_TID,
                   args={"used": m["heap_used"], "free": m["heap_free"]})
        self._emit(ph="C", name="far memory free (KB)", ts=ts, tid=CPU_TID,
                   args={"free": m["far_free_paras"] * 16 // 1024})

    # ── Result ──

    def finish(self):
        """Close whatever is still running and return the trace as a dict."""
        self._close_running(self._end)
        header = [
            {"ph": "M", "name": "process_name", "pid": KERNEL_PID, "tid": CPU_TID,
             "args": {"name": "Phoenix-8086 kernel"}},
            {"ph": "M", "name": "thread_name", "pid": KERNEL_PID, "tid": CPU_TID,
             "args": {"name": "CPU"}},
            {"ph": "M", "name": "thread_name", "pid": KERNEL_PID, "tid": CONSOLE_TID,
             "args": {"name": "console"}},
        ]
        return {
            "traceEvents": header + self.events,
            "displayTimeUnit": "ms",
            "otherData": {"source": "Phoenix-8086 telemetry capture",
                          "time resolution": "context switches: one PIT count (about 0.84 us) "
                                             "where the kernel reports it; other events: "
                                             f"one kernel tick ({1000 // self.hz} ms)"},
        }


def capture_to_trace(entries):
    """Convert (seconds, message) pairs from read_capture() into a trace dict."""
    builder = TraceBuilder()
    for _, message in entries:
        builder.add(message)
    return builder.finish()


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Convert a Phoenix-8086 telemetry capture to a Chrome/Perfetto trace.")
    parser.add_argument("capture", help="capture file written by the bridge or record_session.py")
    parser.add_argument("-o", "--output", help="trace file to write (default: standard output)")
    args = parser.parse_args(argv)

    trace = capture_to_trace(read_capture(args.capture))
    text = json.dumps(trace, separators=(",", ":"))
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"{args.output}: {len(trace['traceEvents'])} events; "
              "open it at https://ui.perfetto.dev", file=sys.stderr)
    else:
        print(text)


if __name__ == "__main__":
    main()
