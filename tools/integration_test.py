#!/usr/bin/env python3
"""
Phoenix-8086 — Integration test

Boots the floppy image headless in QEMU, types shell commands through
the QEMU monitor, and checks the console output mirrored on the serial
port. Covers: shell thread and keyboard input, preemptive scheduling of
several threads, sleep, mailbox IPC, system calls, thread kill, the
in-kernel self-tests, stack overflow detection, and the panic paths
(each panic needs its own boot, because the machine halts).

Usage: integration_test.py build/phoenix8086.img
"""

import os
import re
import subprocess
import sys
import tempfile
import time

TEL_START_BYTE = 0xFE
KEYS = {" ": "spc", "\n": "ret"}


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
    return text.decode("ascii", errors="replace")


class Machine:
    def __init__(self, image, tmp):
        self.serial = os.path.join(tmp, "serial.bin")
        self.qemu = subprocess.Popen(
            ["qemu-system-i386",
             "-drive", f"file={image},format=raw,if=floppy,readonly=on",
             "-boot", "a", "-m", "1M", "-display", "none",
             "-serial", f"file:{self.serial}", "-monitor", "stdio"],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.mark = 0

    def output(self):
        if not os.path.exists(self.serial):
            return ""
        with open(self.serial, "rb") as f:
            return console_text(f.read())

    def type(self, line):
        """Type a command line; later waits only look at output after this point."""
        self.mark = len(self.output())
        for ch in line + "\n":
            self.qemu.stdin.write(f"sendkey {KEYS.get(ch, ch)}\n".encode())
            self.qemu.stdin.flush()
            time.sleep(0.08)

    def wait_for(self, pattern, timeout=15):
        """Wait until the regex appears in the output since the last command."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            text = self.output()[self.mark:]
            if re.search(pattern, text, re.S):
                return text
            time.sleep(0.2)
        return None

    def wait_count(self, needle, count, timeout=15):
        """Wait until a plain string has appeared `count` times since the mark."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            text = self.output()[self.mark:]
            if text.count(needle) >= count:
                return text
            time.sleep(0.2)
        return self.output()[self.mark:]

    def stop(self):
        self.qemu.kill()
        self.qemu.wait()


failures = []


def check(name, ok, detail=""):
    print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
    if not ok:
        failures.append(f"{name}: {detail}")


def run(machine):

    # 1. The shell thread runs and prints its prompt
    check("shell prompt appears", machine.wait_for(r"phoenix> ") is not None)

    # 2. Keyboard input reaches the shell, which blocks between keys
    machine.type("help")
    check("help command", machine.wait_for(r"Shell Commands.*about.*phoenix> ") is not None)

    # 3. Three CPU-bound threads of different priority all make progress
    machine.type("create")
    machine.wait_for(r"TID=")
    machine.type("create")
    machine.wait_for(r"TID=")
    machine.type("create")
    machine.wait_for(r"TID=")
    machine.mark = 0
    for tag in "ACB":
        text = machine.wait_count(f"[{tag}]", 20, timeout=60)
    counts = {t: text.count(f"[{t}]") for t in "ABC"}
    check("demo threads all finish", all(c == 20 for c in counts.values()), str(counts))
    first = {t: text.find(f"[{t}]") for t in "ABC"}
    last_a = text.rfind("[A]")
    check("threads interleave (preemption + aging)",
          0 <= first["B"] < last_a and 0 <= first["C"] < last_a, str(first))

    # 4. Finished threads are gone, shell is still responsive
    machine.type("ps")
    text = machine.wait_for(r"Thread List.*phoenix> ") or ""
    check("exited threads are reaped", "demo-" not in text and "shell" in text, text)

    # 5. Mailbox IPC with blocking receive and sleeping sender
    machine.type("ipc")
    text = machine.wait_for(r"\[ipc done\]") or ""
    check("mailbox delivers 1..5 in order",
          re.search(r"\[recv 1\].*\[recv 2\].*\[recv 3\].*\[recv 4\].*\[recv 5\]", text, re.S) is not None,
          text)

    # 6. System calls through INT 80h, including sleep and tick count
    machine.type("syscall")
    text = machine.wait_for(r"\[syscall done\]") or ""
    slept = re.search(r"\[v1\].*\[slept (\d+) ticks\]", text, re.S)
    check("INT 80h calls work", slept is not None, text)
    check("sleep lasts about 20 ticks", slept is not None and 20 <= int(slept.group(1)) <= 23,
          slept.group(1) if slept else "")

    # 7. Kill a running thread
    time.sleep(0.5)
    machine.type("create")
    text = machine.wait_for(r"TID=(\d+)") or ""
    tid = re.search(r"TID=(\d+)", text)
    if tid:
        machine.type(f"kill {tid.group(1)}")
        check("kill command", machine.wait_for(r"Killing thread") is not None)
        machine.type("ps")
        text = machine.wait_for(r"Thread List.*phoenix> ") or ""
        check("killed thread is gone", "demo-" not in text, text)
    else:
        check("kill command", False, "no thread created")

    # 8. Counters are real
    machine.type("stats")
    text = machine.wait_for(r"Runtime Statistics.*phoenix> ") or ""
    switches = re.search(r"Context switches: (\d+)", text)
    syscalls = re.search(r"System calls:\s+(\d+)", text)
    check("context switches counted", switches is not None and int(switches.group(1)) > 20, text)
    check("system calls counted", syscalls is not None and int(syscalls.group(1)) >= 8, text)

    check("no stack overflow reported", "STACK OVERFLOW" not in machine.output())

    # 9. In-kernel unit tests: heap, far arena, semaphore, mutex, mailbox, sleep, syscalls
    machine.type("selftest")
    text = machine.wait_for(r"selftest: \d+ passed, \d+ failed") or ""
    result = re.search(r"selftest: (\d+) passed, (\d+) failed", text)
    check("kernel self-tests pass",
          result is not None and int(result.group(1)) >= 30 and result.group(2) == "0", text)

    # 10. Memory map reports the real layout
    machine.type("memory")
    text = machine.wait_for(r"Memory Map.*Far free:\s+\d+ KB.*phoenix> ") or ""
    far = re.search(r"Far free:\s+(\d+) KB", text)
    check("memory map shows kernel segments",
          "0x1000:0x0000" in text and "0x2000:0x0000" in text and "0x3000:0x0000" in text, text)
    check("far arena has memory", far is not None and int(far.group(1)) >= 400, text)

    # 11. Priority change and shell sleep
    machine.type("create")
    text = machine.wait_for(r"TID=(\d+)") or ""
    tid = re.search(r"TID=(\d+)", text)
    machine.type(f"nice {tid.group(1) if tid else 9} 9")
    check("nice command", machine.wait_for(r"priority set to 9") is not None)
    machine.type("sleep 30")
    text = machine.wait_for(r"Slept (\d+) ticks") or ""
    slept = re.search(r"Slept (\d+) ticks", text)
    check("shell sleep", slept is not None and 30 <= int(slept.group(1)) <= 33, text)
    time.sleep(4)   # let the demo thread finish

    # 12. Benchmark produces plausible numbers
    machine.type("bench")
    text = machine.wait_for(r"kmalloc\+kfree pairs/sec, (under 1|\d+) us each", timeout=20) or ""
    switches = re.search(r"bench: (\d+) context switches/sec", text)
    check("benchmark runs", switches is not None and int(switches.group(1)) > 100, text)

    # 13. A runaway recursion is killed before it damages another stack
    machine.type("overflow")
    text = machine.wait_for(r"STACK OVERFLOW: Thread \d+ \(overflow\)") or ""
    check("stack overflow detected", text != "")
    time.sleep(0.5)
    machine.type("ps")
    text = machine.wait_for(r"Thread List.*phoenix> ") or ""
    check("shell survives the overflow", "shell" in text and "overflow" not in text, text)
    machine.type("selftest")
    text = machine.wait_for(r"selftest: \d+ passed, \d+ failed") or ""
    check("self-tests still pass afterwards", ", 0 failed" in text, text)


def run_panic(machine, command, reason):
    """Boot, run a command that must end in a panic screen with live registers."""
    if machine.wait_for(r"phoenix> ") is None:
        check(f"{command}: boot", False)
        return
    machine.type(command)
    text = machine.wait_for(r"KERNEL PANIC.*Press RESET") or ""
    check(f"{command}: panic screen", reason in text and "Name=shell" in text, text)
    # CS must be the kernel code segment and IP non-zero: registers are live, not stale
    regs = re.search(r"CS=0x([0-9A-F]{4})\s+IP=0x([0-9A-F]{4})", text)
    check(f"{command}: live register dump",
          regs is not None and regs.group(1) == "1000" and regs.group(2) != "0000", text)


def main():
    image = sys.argv[1]
    scenarios = [
        ("main", run),
        ("panic", lambda m: run_panic(m, "panic", "User-triggered panic")),
        ("divzero", lambda m: run_panic(m, "divzero", "Divide error")),
    ]
    for name, scenario in scenarios:
        with tempfile.TemporaryDirectory() as tmp:
            machine = Machine(image, tmp)
            try:
                scenario(machine)
            finally:
                machine.stop()

    if failures:
        print(f"Integration test FAILED ({len(failures)}):")
        for failure in failures:
            print(f"  - {failure[:600]}")
        sys.exit(1)
    print("  Integration test passed")


if __name__ == "__main__":
    main()
