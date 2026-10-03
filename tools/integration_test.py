#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Integration test

Boots the floppy image headless in QEMU, types shell commands through
the QEMU monitor, and checks the console output and the telemetry
records decoded from the serial port. Covers: shell thread and keyboard input, preemptive scheduling of
several threads, sleep, mailbox IPC, system calls, thread kill, the
in-kernel self-tests, stack overflow detection, and the panic paths
(each panic needs its own boot, because the machine halts).

Usage: integration_test.py build/phoenix8086.img
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

KEYS = {" ": "spc", "\n": "ret", ".": "dot", "-": "minus", "/": "slash"}


class Machine:
    """A headless QEMU with its serial port on a socket, decoded as telemetry."""

    def __init__(self, image, tmp):
        path = os.path.join(tmp, "serial.sock")
        self.qemu = subprocess.Popen(
            ["qemu-system-i386",
             "-drive", f"file={image},format=raw,if=floppy,readonly=on",
             "-boot", "a", "-m", "1M", "-display", "none",
             "-serial", f"unix:{path},server=on,wait=off", "-monitor", "stdio"],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # Connect before the BIOS hands over to the kernel, so nothing is missed
        self.sock = socket.socket(socket.AF_UNIX)
        for _ in range(100):
            try:
                self.sock.connect(path)
                break
            except OSError:
                time.sleep(0.02)
        else:
            raise RuntimeError("could not connect to the QEMU serial socket")

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

    def telemetry(self, type_name=None):
        with self.lock:
            return [m for m in self.messages if type_name in (None, m["type"])]

    def type(self, line):
        """Type a command line on the keyboard; later waits only look at output after this point."""
        self.mark = len(self.output())
        for ch in line + "\n":
            self.qemu.stdin.write(f"sendkey {KEYS.get(ch, ch)}\n".encode())
            self.qemu.stdin.flush()
            time.sleep(0.08)

    def send_serial(self, line):
        """Type a command line through the serial input channel instead of the keyboard."""
        self.mark = len(self.output())
        self.sock.sendall(line.encode() + b"\r")

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
        self.sock.close()


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

    # Keyboard layouts: QEMU's "y" key is the key a German keyboard labels Z
    machine.type("keymap de")
    check("keymap command", machine.wait_for(r"Keymap: de") is not None)
    machine.type("y")
    check("German layout: the Y key types z", machine.wait_for(r"Unknown command: z\n") is not None)
    machine.type("kezmap us")       # typed on the German layout: its Z key gives y
    machine.wait_for(r"Keymap: us")
    machine.type("y")
    check("US layout restored", machine.wait_for(r"Unknown command: y\n") is not None)

    # QEMU is a 386 or later. The 8086 fidelity test expects the opposite answer.
    machine.type("cpu")
    text = machine.wait_for(r"CPU: [^\n]+\n") or ""
    check("CPU probe reports 286 or later under QEMU", "CPU: 80286 or later" in text, text)

    # 8. Counters are real
    machine.type("stats")
    text = machine.wait_for(r"Runtime Statistics.*phoenix> ") or ""
    switches = re.search(r"Context switches: (\d+)", text)
    syscalls = re.search(r"System calls:\s+(\d+)", text)
    check("context switches counted", switches is not None and int(switches.group(1)) > 20, text)
    check("system calls counted", syscalls is not None and int(syscalls.group(1)) >= 8, text)

    check("no stack overflow reported", "STACK OVERFLOW" not in machine.output())

    # Telemetry must agree with the kernel's own counters (nothing invented, nothing lost)
    time.sleep(0.5)
    counters = machine.telemetry("COUNTERS")[-1]
    reported = sum(1 for m in machine.telemetry("CONTEXT_SWITCH") if m["tick"] <= counters["tick"])
    check("telemetry: no records dropped", counters["drops"] == 0, str(counters))
    check("telemetry: every context switch is reported",
          0 <= reported - counters["context_switches"] <= 2,
          f"{reported} records vs counter {counters['context_switches']}")
    hello = machine.telemetry("HELLO")[-1]
    check("telemetry: HELLO describes the machine",
          hello["code_seg"] == 0x1000 and hello["data_seg"] == 0x2000 and hello["hz"] == 100, str(hello))
    names = {m["name"] for m in machine.telemetry("THREAD_STATS")}
    check("telemetry: thread statistics", {"idle", "shell", "telemetry"} <= names, str(names))
    created = {m["name"] for m in machine.telemetry("THREAD_CREATE")}
    check("telemetry: thread creation", {"demo-A", "consumer", "syscall"} <= created, str(created))
    exits = len(machine.telemetry("THREAD_EXIT"))
    check("telemetry: thread exit", exits >= 7, str(exits))
    states = {m["state"] for m in machine.telemetry("THREAD_STATE")}
    check("telemetry: state changes", {"BLOCKED", "SLEEPING", "READY"} <= states, str(states))
    check("telemetry: system calls", len(machine.telemetry("SYSCALL")) >= 8)
    switch = machine.telemetry("CONTEXT_SWITCH")[-1]
    check("telemetry: switch carries the resumed registers",
          switch["regs"]["cs"] == 0x1000 and switch["regs"]["ip"] != 0, str(switch))

    # Serial input channel: a dashboard can type into the shell
    machine.send_serial("ticks")
    check("serial input reaches the shell", machine.wait_for(r"Ticks: \d+") is not None)

    # FAT12 boot disk: directory, text file, programs built with the SDK
    machine.type("memory")
    text = machine.wait_for(r"Far free:\s+\d+ KB.*phoenix> ") or ""
    before = re.search(r"Heap free:\s+(\d+).*Far free:\s+(\d+) KB", text, re.S)

    machine.type("ls")
    text = machine.wait_for(r"\d+ file\(s\)", timeout=30) or ""
    check("ls lists the files on the FAT12 disk",
          all(name in text for name in ("README.TXT", "HELLO.BIN", "PRIMES.BIN", "CLOCK.BIN",
                                        "THREADS.BIN")), text)
    machine.type("cat readme.txt")
    check("cat prints a file", machine.wait_for(r"Phoenix-8086 boot disk.*docs/programs\.md", timeout=30) is not None)

    machine.type("run hello.bin")
    check("program: hello", machine.wait_for(r"Hello from a program loaded off the disk!.*ABI version 1\n",
                                             timeout=30) is not None)
    machine.type("run primes.bin")
    text = machine.wait_for(r"last digit \w+\n", timeout=30) or ""
    check("program: primes (bss, data, pointer table, jump table)",
          "primes below 1000: 168" in text and "largest 997" in text and "last digit seven" in text, text)
    machine.type("run clock.bin")
    text = machine.wait_for(r"first line of README\.TXT: [^\n]*\n", timeout=30) or ""
    elapsed = re.search(r"elapsed ticks: (\d+)", text)
    check("program: clock (sleep, ticks, file calls)",
          elapsed is not None and 75 <= int(elapsed.group(1)) <= 80 and
          "first line of README.TXT: Phoenix-8086 boot disk" in text, text)

    machine.type("run threads.bin")
    text = machine.wait_for(r"threads: two workers sent \d+ numbers, total \d+\n", timeout=30) or ""
    check("program: threads (a program starts threads in its own code; mailbox, semaphore)",
          "two workers sent 10 numbers, total 1515" in text, text)

    machine.type("run readme.txt")
    check("a non-program file is rejected", machine.wait_for(r"not a Phoenix-8086 program") is not None)
    machine.type("run missing.bin")
    check("a missing program is reported", machine.wait_for(r"file not found") is not None)

    time.sleep(1.0)
    machine.type("memory")
    text = machine.wait_for(r"Far free:\s+\d+ KB.*phoenix> ") or ""
    after = re.search(r"Heap free:\s+(\d+).*Far free:\s+(\d+) KB", text, re.S)
    check("programs give their memory back, including far memory left allocated",
          before is not None and after is not None and before.groups() == after.groups(),
          f"{before and before.groups()} → {after and after.groups()}")
    programs = {m["name"] for m in machine.telemetry("THREAD_CREATE")}
    check("telemetry: program threads", {"hello", "primes", "clock", "threads"} <= programs, str(programs))

    # 9. In-kernel unit tests: heap, far arena, semaphore, mutex, mailbox, sleep, syscalls, files
    machine.type("selftest")
    text = machine.wait_for(r"selftest: \d+ passed, \d+ failed") or ""
    result = re.search(r"selftest: (\d+) passed, (\d+) failed", text)
    check("kernel self-tests pass",
          result is not None and int(result.group(1)) >= 65 and result.group(2) == "0", text)

    # The remaining informational commands
    machine.type("about")
    check("about command", machine.wait_for(r"Version [\w.-]+.*Threads: \d+.*Uptime: \d+s") is not None)
    machine.type("uptime")
    check("uptime command", machine.wait_for(r"Uptime: \d+m \d+s") is not None)
    machine.type("interrupts")
    text = machine.wait_for(r"Interrupt Counters.*Context Switches: \d+") or ""
    timer = re.search(r"Timer \(IRQ0\):\s+(\d+)", text)
    keyboard = re.search(r"Keyboard \(IRQ1\):\s+(\d+)", text)
    check("interrupts command", timer is not None and int(timer.group(1)) > 100 and
          keyboard is not None and int(keyboard.group(1)) > 50, text)
    machine.type("scheduler")
    text = machine.wait_for(r"Scheduler Queue.*Ready threads:.*phoenix> ") or ""
    check("scheduler command", "Current: TID 1" in text and "idle" in text, text)
    machine.type("registers")
    text = machine.wait_for(r"Register Dump.*FLAGS=0x[0-9A-F]{4}") or ""
    check("registers command",
          re.search(r"CS=0x1000\s+DS=0x2000\s+ES=0x[0-9A-F]{4}\s+SS=0x2000", text) is not None, text)
    machine.type("clear")
    time.sleep(0.5)
    machine.type("ticks")
    check("clear command leaves the shell working", machine.wait_for(r"Ticks: \d+") is not None)

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
    time.sleep(0.5)
    bench = {m["kind"]: m["count"] for m in machine.telemetry("BENCH")}
    check("telemetry: benchmark results",
          switches is not None and bench.get("context_switches") == int(switches.group(1))
          and "heap_pairs" in bench, str(bench))

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

    # The stream itself must be clean: no damaged frames, no sequence gaps
    decoder = machine.decoder
    check("telemetry: stream intact",
          decoder.bad_frames == 0 and decoder.lost_frames == 0 and decoder.stray_bytes == 0,
          f"bad={decoder.bad_frames} lost={decoder.lost_frames} stray={decoder.stray_bytes}")


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
    time.sleep(0.3)
    faults = machine.telemetry("FAULT")
    check(f"{command}: fault record in telemetry",
          len(faults) == 1 and reason in faults[0]["reason"] and faults[0]["regs"]["cs"] == 0x1000,
          str(faults))


def run_reboot(machine):
    """The reboot command restarts the machine and the kernel boots again."""
    if machine.wait_for(r"phoenix> ") is None:
        check("reboot: boot", False)
        return
    machine.type("reboot")
    check("reboot: the command announces itself", machine.wait_for(r"Rebooting") is not None)
    machine.mark = len(machine.output())
    check("reboot: the kernel boots a second time",
          machine.wait_for(r"boot complete.*phoenix> ", timeout=40) is not None,
          machine.output()[machine.mark:][-200:])
    stages = [m["stage"] for m in machine.telemetry("BOOT_STAGE")]
    check("reboot: boot stages are reported twice", stages.count(7) == 2, str(stages))
    machine.type("ticks")
    check("reboot: the shell works afterwards", machine.wait_for(r"Ticks: \d+") is not None)
    decoder = machine.decoder
    check("reboot: telemetry stays consistent across the restart",
          decoder.bad_frames == 0 and decoder.lost_frames == 0,
          f"bad={decoder.bad_frames} lost={decoder.lost_frames}")


def run_outside_program(image, program):
    """
    Copy a program onto a copy of the image with mtools, an independent
    FAT implementation, and run it. Shows the disk is an ordinary FAT12
    volume that tools other than ours can write to.
    """
    mcopy = shutil.which("mcopy")
    if not mcopy:
        print("  skip  mcopy (mtools) not installed: outside-tool test not run")
        return
    with tempfile.TemporaryDirectory() as tmp:
        copy = os.path.join(tmp, "floppy.img")
        shutil.copyfile(image, copy)
        result = subprocess.run([mcopy, "-i", copy, program, "::OUTSIDE.BIN"],
                                capture_output=True, text=True)
        check("mcopy adds a file to the image", result.returncode == 0, result.stderr)
        if result.returncode != 0:
            return
        machine = Machine(copy, tmp)
        try:
            machine.wait_for(r"phoenix> ")
            machine.type("ls")
            text = machine.wait_for(r"\d+ file\(s\)", timeout=30) or ""
            check("kernel sees the file added by mcopy", "OUTSIDE.BIN" in text, text)
            machine.type("run outside.bin")
            check("kernel runs the program added by mcopy",
                  machine.wait_for(r"Hello from a program loaded off the disk!", timeout=30) is not None)
        finally:
            machine.stop()


def main():
    image = sys.argv[1]
    scenarios = [
        ("main", run),
        ("panic", lambda m: run_panic(m, "panic", "User-triggered panic")),
        ("divzero", lambda m: run_panic(m, "divzero", "Divide error")),
        ("reboot", run_reboot),
    ]
    for name, scenario in scenarios:
        with tempfile.TemporaryDirectory() as tmp:
            machine = Machine(image, tmp)
            try:
                scenario(machine)
            finally:
                machine.stop()

    program = os.path.join(os.path.dirname(image), "programs", "HELLO.BIN")
    run_outside_program(image, program)

    if failures:
        print(f"Integration test FAILED ({len(failures)}):")
        for failure in failures:
            print(f"  - {failure[:600]}")
        sys.exit(1)
    print("  Integration test passed")


if __name__ == "__main__":
    main()
