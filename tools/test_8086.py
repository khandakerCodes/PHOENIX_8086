#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — 8086 fidelity test

QEMU emulates a 386 or later, so passing there does not show that the
kernel runs on an 8086. This test boots the same floppy image in
DOSBox-X with its CPU set to 8086, where 186+ instructions do not
exist, and drives the shell through the serial input channel.

It checks that the kernel itself identifies the processor as an
8086/8088, runs the in-kernel self-tests, and exercises preemptive
scheduling, mailbox IPC and system calls.

Needs DOSBox-X (sudo apt install dosbox-x), or set DOSBOX_X to its path.

Usage: test_8086.py build/phoenix8086.img
"""

import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from bridge.protocol import Decoder  # noqa: E402

CONFIG = """\
[dosbox]
machine=cga
memsize=1
memsizekb=640

[cpu]
core=normal
cputype=8086
cycles=fixed 5000

[serial]
serial1=nullmodem port:{port} transparent:1

[autoexec]
mount c "{directory}"
c:
boot PHX.IMG
"""


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Machine:
    """DOSBox-X as an 8086, with the kernel's serial port on a TCP socket."""

    def __init__(self, dosbox, image, tmp):
        port = free_port()
        shutil.copyfile(image, os.path.join(tmp, "PHX.IMG"))
        config = os.path.join(tmp, "dosbox-x.conf")
        with open(config, "w") as f:
            f.write(CONFIG.format(port=port, directory=tmp))

        self.process = subprocess.Popen(
            [dosbox, "-silent", "-nopromptfolder", "-conf", config],
            cwd=tmp, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        self.sock = None
        for _ in range(150):
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except OSError:
                if self.process.poll() is not None:
                    raise RuntimeError("DOSBox-X exited before opening its serial port")
                time.sleep(0.1)
        if self.sock is None:
            raise RuntimeError("could not connect to the DOSBox-X serial port")
        self.sock.settimeout(None)

        self.decoder = Decoder()
        self.messages = []
        self.text = ""
        self.lock = threading.Lock()
        self.mark = 0
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            try:
                data = self.sock.recv(4096)
            except OSError:
                return
            if not data:
                return
            with self.lock:
                for message in self.decoder.feed(data):
                    self.messages.append(message)
                    if message["type"] == "CONSOLE":
                        self.text += message["text"]

    def output(self):
        with self.lock:
            return self.text

    def telemetry(self, type_name):
        with self.lock:
            return [m for m in self.messages if m["type"] == type_name]

    def command(self, line):
        """Type a command through the kernel's serial input channel."""
        self.mark = len(self.output())
        self.sock.sendall(line.encode() + b"\r")

    def wait_for(self, pattern, timeout=60):
        deadline = time.time() + timeout
        while time.time() < deadline:
            text = self.output()[self.mark:]
            if re.search(pattern, text, re.S):
                return text
            time.sleep(0.2)
        return None

    def wait_count(self, needle, count, timeout=60):
        deadline = time.time() + timeout
        while time.time() < deadline:
            text = self.output()[self.mark:]
            if text.count(needle) >= count:
                return text
            time.sleep(0.2)
        return self.output()[self.mark:]

    def stop(self):
        self.process.kill()
        self.process.wait()
        if self.sock:
            self.sock.close()


failures = []


def check(name, ok, detail=""):
    print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
    if not ok:
        failures.append(f"{name}: {detail}")


def run(machine):
    check("boots to the shell prompt on an 8086",
          machine.wait_for(r"boot complete.*phoenix> ") is not None, machine.output()[-300:])

    # The kernel probes the processor with 8086-legal instructions. This is
    # also the control for the test itself: under QEMU the answer is different.
    machine.command("cpu")
    text = machine.wait_for(r"CPU: [^\n]+\n") or ""
    check("kernel identifies the CPU as 8086/8088", "CPU: 8086/8088" in text, text)

    machine.command("selftest")
    text = machine.wait_for(r"selftest: \d+ passed, \d+ failed") or ""
    result = re.search(r"selftest: (\d+) passed, (\d+) failed", text)
    check("kernel self-tests pass",
          result is not None and int(result.group(1)) >= 45 and result.group(2) == "0", text)

    for _ in range(3):
        machine.command("create")
        machine.wait_for(r"TID=")
    machine.mark = 0
    for tag in "ACB":
        text = machine.wait_count(f"[{tag}]", 20, timeout=90)
    counts = {tag: text.count(f"[{tag}]") for tag in "ABC"}
    check("three threads are preempted and all finish", all(c == 20 for c in counts.values()),
          str(counts))
    first = {tag: text.find(f"[{tag}]") for tag in "ABC"}
    check("their output interleaves",
          0 <= first["B"] < text.rfind("[A]") and 0 <= first["C"] < text.rfind("[A]"), str(first))

    time.sleep(0.5)
    machine.command("ipc")
    text = machine.wait_for(r"\[ipc done\]") or ""
    check("mailbox IPC delivers 1..5 in order",
          re.search(r"\[recv 1\].*\[recv 2\].*\[recv 3\].*\[recv 4\].*\[recv 5\]", text, re.S) is not None,
          text)

    machine.command("syscall")
    text = machine.wait_for(r"\[syscall done\]") or ""
    slept = re.search(r"\[v1\].*\[slept (\d+) ticks\]", text, re.S)
    check("INT 80h system calls work", slept is not None and 20 <= int(slept.group(1)) <= 23, text)

    # Disk reads go through this emulator's BIOS, a different one from QEMU's
    machine.command("cat readme.txt")
    check("reads a file from the FAT12 disk",
          machine.wait_for(r"Phoenix-8086 boot disk.*docs/programs\.md") is not None)
    machine.command("run primes.bin")
    text = machine.wait_for(r"last digit \w+\n") or ""
    check("loads and runs a program from the disk",
          "primes below 1000: 168" in text and "largest 997" in text and "last digit seven" in text, text)
    machine.command("run clock.bin")
    text = machine.wait_for(r"first line of README\.TXT: [^\n]*\n") or ""
    check("a program sleeps and reads a file",
          "first line of README.TXT: Phoenix-8086 boot disk" in text, text)

    time.sleep(0.5)
    machine.command("ps")
    text = machine.wait_for(r"Thread List.*phoenix> ") or ""
    check("shell is responsive and threads were reaped",
          "shell" in text and "demo-" not in text, text)

    check("no fault or stack overflow",
          not machine.telemetry("FAULT") and "STACK OVERFLOW" not in machine.output(),
          str(machine.telemetry("FAULT")))
    decoder = machine.decoder
    check("telemetry stream intact", decoder.bad_frames == 0 and decoder.lost_frames == 0,
          f"bad={decoder.bad_frames} lost={decoder.lost_frames}")
    switches = len(machine.telemetry("CONTEXT_SWITCH"))
    check("context switches happened", switches > 50, str(switches))


def main():
    image = sys.argv[1]
    dosbox = os.environ.get("DOSBOX_X") or shutil.which("dosbox-x")
    if not dosbox:
        sys.exit("DOSBox-X not found. Install it (sudo apt install dosbox-x) "
                 "or set DOSBOX_X to its path.")

    with tempfile.TemporaryDirectory() as tmp:
        machine = Machine(dosbox, image, tmp)
        try:
            run(machine)
        finally:
            machine.stop()

    if failures:
        print(f"8086 fidelity test FAILED ({len(failures)}):")
        for failure in failures:
            print(f"  - {failure[:600]}")
        sys.exit(1)
    print("  8086 fidelity test passed")


if __name__ == "__main__":
    main()
