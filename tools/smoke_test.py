#!/usr/bin/env python3
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

EXPECTED = [b"Phoenix-8086 Microkernel", b"Interrupt system... OK", b"boot complete"]
TIMEOUT = 20
TEL_START_BYTE = 0xFE


def console_text(stream):
    """Drop telemetry frames ([0xFE][type][len][data][checksum]) from the serial stream."""
    text = bytearray()
    i = 0
    while i < len(stream):
        if stream[i] == TEL_START_BYTE:
            if i + 2 >= len(stream):
                break
            i += 3 + stream[i + 2] + 1
        else:
            text.append(stream[i])
            i += 1
    return bytes(text)


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
