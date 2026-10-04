#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Soak test

Runs the kernel in QEMU for a set time under constant churn: threads
created, preempted, killed and reaped; mailbox IPC; system calls;
programs loaded from disk and unloaded. After every cycle it checks
that nothing leaked or broke:

  - heap and far memory are back at their starting values
  - only the three permanent threads remain
  - no panic, no stack overflow
  - no damaged, lost or dropped telemetry records
  - the timer is still ticking

Usage: soak_test.py build/phoenix8086.img [seconds]     (default 120)
"""

import os
import re
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from tools.integration_test import Machine  # noqa: E402

PERMANENT_THREADS = {"idle", "shell", "telemetry"}
SELFTEST_EVERY = 5


class SoakFailure(Exception):
    pass


def expect(condition, message):
    if not condition:
        raise SoakFailure(message)


def command(machine, line, pattern, timeout=60):
    """Type a command over the serial input channel and wait for its output."""
    machine.send_serial(line)
    text = machine.wait_for(pattern, timeout=timeout)
    expect(text is not None, f"'{line}' did not produce /{pattern}/ within {timeout}s; "
                             f"got: {machine.output()[machine.mark:][-300:]!r}")
    return text


def memory(machine):
    text = command(machine, "memory", r"Far free:\s+\d+ KB.*phoenix> ")
    match = re.search(r"Heap free:\s+(\d+) bytes.*Far free:\s+(\d+) KB", text, re.S)
    return int(match.group(1)), int(match.group(2))


def wait_until_quiet(machine, timeout=90):
    """Wait until only the permanent threads are left."""
    deadline = time.time() + timeout
    names = set()
    while time.time() < deadline:
        text = command(machine, "ps", r"Thread List.*phoenix> ")
        # Table rows: "│   1    shell   ■ running ..." (the state follows a coloured dot)
        names = set(re.findall(r"\s\d+\s+(\S+)\s+\S (?:ready|running|blocked|sleeping)\b", text))
        if names == PERMANENT_THREADS:
            return
        time.sleep(1)
    raise SoakFailure(f"threads did not finish: {sorted(names - PERMANENT_THREADS)}")


def cycle(machine, number):
    # CPU-bound threads at three priorities, plus a program, all preempting each other
    for _ in range(3):
        command(machine, "create", r"TID=\d+")
    command(machine, "run primes.bin", r"Started")
    wait_until_quiet(machine)
    expect("largest 997" in machine.output(), "primes program printed the wrong result")

    # Blocking IPC, system calls, a sleeping program, and a thread killed mid-run
    command(machine, "ipc", r"TID=\d+.*TID=\d+")
    command(machine, "syscall", r"TID=\d+")
    command(machine, "run clock.bin", r"Started")
    wait_until_quiet(machine)

    # A program with several threads of its own, sharing its memory
    command(machine, "run threads.bin", r"Started")
    victim = re.search(r"TID=(\d+)", command(machine, "create", r"TID=\d+")).group(1)
    command(machine, f"kill {victim}", r"Killing thread")
    wait_until_quiet(machine)
    output = machine.output()
    expect("[ipc done]" in output and "[syscall done]" in output and
           "first line of README.TXT: Phoenix-8086 boot disk" in output and
           "total 1515" in output,
           "a demo did not complete")

    if number % SELFTEST_EVERY == 0:
        text = command(machine, "selftest", r"selftest: \d+ passed, \d+ failed")
        expect(", 0 failed" in text, f"self-test failed: {text[-200:]!r}")


def check_health(machine, baseline, last_tick):
    expect(not machine.telemetry("FAULT"), f"kernel panic: {machine.telemetry('FAULT')}")
    expect("STACK OVERFLOW" not in machine.output(), "a stack overflow was reported")

    decoder = machine.decoder
    expect(decoder.bad_frames == 0 and decoder.lost_frames == 0,
           f"telemetry damaged: bad={decoder.bad_frames} lost={decoder.lost_frames}")

    counters = machine.telemetry("COUNTERS")
    expect(counters, "no COUNTERS records received")
    expect(counters[-1]["drops"] == 0, f"kernel dropped {counters[-1]['drops']} telemetry records")
    expect(counters[-1]["timer"] > last_tick, "the timer stopped ticking")

    now = memory(machine)
    expect(now == baseline, f"memory leak: heap/far free went from {baseline} to {now}")
    return counters[-1]


def forget(machine):
    """Drop what has been checked, so an hour-long run does not hoard messages."""
    with machine.lock:
        machine.messages.clear()
        machine.text = ""
    machine.mark = 0


def main():
    image = sys.argv[1]
    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 120

    with tempfile.TemporaryDirectory() as tmp:
        machine = Machine(image, tmp)
        cycles = 0
        counters = {"timer": 0, "context_switches": 0}
        try:
            expect(machine.wait_for(r"phoenix> ") is not None, "kernel did not reach the shell")
            baseline = memory(machine)
            started = time.time()

            while time.time() - started < seconds:
                cycles += 1
                cycle(machine, cycles)
                counters = check_health(machine, baseline, counters["timer"])
                forget(machine)
                print(f"  cycle {cycles}: {int(time.time() - started)}s, "
                      f"{counters['context_switches']} context switches, memory steady",
                      flush=True)
        except SoakFailure as failure:
            print(f"Soak test FAILED in cycle {cycles}: {failure}")
            sys.exit(1)
        finally:
            machine.stop()

    print(f"  Soak test passed: {cycles} cycles in {seconds}s, "
          f"{counters['context_switches']} context switches, "
          f"{counters['timer']} ticks, no leaks, faults or lost telemetry")


if __name__ == "__main__":
    main()
