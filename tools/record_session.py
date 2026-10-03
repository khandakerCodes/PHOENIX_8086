#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Record a telemetry capture

Boots the floppy image headless in QEMU, types a short scripted tour
of the shell, and writes everything the kernel reported to a capture
file. The capture can be replayed to the dashboard without a kernel:

    python3 bridge/serial_ws_bridge.py --replay <capture>

Usage: record_session.py build/phoenix8086.img out.jsonl
"""

import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from bridge.capture import CaptureWriter  # noqa: E402
from tools.integration_test import Machine  # noqa: E402

# (command, regex to wait for, seconds to linger afterwards)
TOUR = [
    ("create", r"TID=", 0.3),
    ("create", r"TID=", 0.3),
    ("create", r"TID=", 3.0),
    ("ps", r"Thread List.*phoenix> ", 0.5),
    ("ipc", r"\[ipc done\]", 0.5),
    ("syscall", r"\[syscall done\]", 0.5),
    ("memory", r"Far free", 0.5),
    ("panic", r"Press RESET", 0.5),
]


def main():
    image, out = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory() as tmp:
        machine = Machine(image, tmp)
        started = time.monotonic()
        try:
            if machine.wait_for(r"phoenix> ") is None:
                sys.exit("kernel did not reach the shell prompt")
            for command, pattern, linger in TOUR:
                machine.type(command)
                if machine.wait_for(pattern, timeout=30) is None:
                    sys.exit(f"'{command}' did not produce the expected output")
                time.sleep(linger)
        finally:
            machine.stop()

    # Message arrival times are not kept by Machine; rebuild them from kernel ticks
    messages = machine.telemetry()
    hz = next((m["hz"] for m in messages if m["type"] == "HELLO"), 100)
    writer = CaptureWriter(out)
    for message in messages:
        writer.write(message["tick"] / hz, message)
    writer.close()

    decoder = machine.decoder
    print(f"  Recorded {len(messages)} messages over {time.monotonic() - started:.1f}s "
          f"(bad={decoder.bad_frames}, lost={decoder.lost_frames}) to {out}")


if __name__ == "__main__":
    main()
