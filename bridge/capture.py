# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Telemetry capture files

A capture is a JSON Lines file. The first line is a header:

    {"capture": 1, "created": "2026-10-03T12:00:00+00:00"}

Every other line is one decoded telemetry message with the time, in
seconds since the capture started, at which the bridge received it:

    {"t": 1.234, "msg": {"type": "CONTEXT_SWITCH", ...}}
"""

import datetime
import json

CAPTURE_VERSION = 1


class CaptureWriter:
    def __init__(self, path):
        self._file = open(path, "w", encoding="utf-8")
        header = {"capture": CAPTURE_VERSION,
                  "created": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")}
        self._file.write(json.dumps(header) + "\n")

    def write(self, elapsed, message):
        self._file.write(json.dumps({"t": round(elapsed, 4), "msg": message}) + "\n")
        self._file.flush()

    def close(self):
        self._file.close()


def read_capture(path):
    """Return the list of (seconds, message) pairs in a capture file."""
    entries = []
    with open(path, encoding="utf-8") as f:
        header = json.loads(f.readline())
        if header.get("capture") != CAPTURE_VERSION:
            raise ValueError(f"{path}: not a version {CAPTURE_VERSION} capture file")
        for line in f:
            line = line.strip()
            if line:
                entry = json.loads(line)
                entries.append((float(entry["t"]), entry["msg"]))
    return entries
