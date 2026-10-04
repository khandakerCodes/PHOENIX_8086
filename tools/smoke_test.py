#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Boot smoke test

Boots the floppy image headless in QEMU, captures the serial port
(the console is mirrored there), and checks that the kernel reached
the end of its boot sequence.

Usage: smoke_test.py build/phoenix8086.img
"""

import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from bridge.protocol import Decoder  # noqa: E402

EXPECTED = [b"a preemptive kernel for the Intel 8086", b"timer 100 Hz, keyboard, INT 80h", b"boot complete"]
TIMEOUT = 20


def console_text(stream):
    """
    Console text from the serial stream: the CONSOLE telemetry records,
    or the raw bytes if the kernel was built with TELEMETRY=0.
    """
    decoder = Decoder()
    messages = decoder.feed(stream)
    if decoder.frames == 0:
        return bytes(stream)
    return "".join(m["text"] for m in messages if m["type"] == "CONSOLE").encode()


def main():
    image = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        serial = os.path.join(tmp, "serial.bin")
        qemu = subprocess.Popen(
            ["qemu-system-i386",
             "-drive", f"file={image},format=raw,if=floppy,readonly=on",
             "-boot", "a", "-m", "1M", "-display", "none",
             "-serial", f"file:{serial}", "-monitor", "none"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        data = b""
        try:
            deadline = time.time() + TIMEOUT
            while time.time() < deadline and qemu.poll() is None:
                time.sleep(0.5)
                if os.path.exists(serial):
                    with open(serial, "rb") as f:
                        data = console_text(f.read())
                if all(marker in data for marker in EXPECTED):
                    break
        finally:
            qemu.kill()
            qemu.wait()

    missing = [m.decode() for m in EXPECTED if m not in data]
    if missing:
        print(f"Smoke test FAILED: missing {missing} ({len(data)} console bytes)")
        sys.exit(1)
    print(f"  Smoke test passed: kernel booted ({len(data)} console bytes)")


if __name__ == "__main__":
    main()
