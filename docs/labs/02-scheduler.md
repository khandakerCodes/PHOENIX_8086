# Lab 2 — Change the Scheduler

## Goal

Measure how the scheduler shares the CPU, then change its policy and measure again.

You will learn what priorities, time slices and aging each contribute, and how to tell a scheduling policy's effect from its description.

## Background

Read the "Scheduling" section of the [architecture guide](../architecture.md) and then `select_next_thread`, `age_ready_threads` and `sched_switch` in `kernel/scheduler.c`. The constants are in `kernel/scheduler.h`:

* `TIME_SLICE_TICKS` — how long a thread runs before an equal-priority thread gets a turn
* `AGING_TICKS` — how long a thread must wait to gain one level of effective priority

The `create` command starts CPU-bound demo threads at priorities 5, 3 and 4 (demo-A, demo-B, demo-C, in that order). Each prints its letter twenty times, staying busy for a tenth of a second per letter.

## Part 1 — Measure the baseline

1. Boot (`make run`) and type `create` three times, quickly.
2. Write down the order of the `[A]`, `[B]` and `[C]` output.
3. While they run, type `ps` and note each thread's CPU ticks.
4. For exact numbers, record the session and compute each thread's share:

   ```sh
   python3 tools/record_session.py build/phoenix8086.img baseline.jsonl
   ```

   The capture holds every `CONTEXT_SWITCH` with its tick. Write a short script that sums, per thread, the ticks between a switch to it and the next switch.

**Predict before you look:** with priorities 5, 3 and 4, and aging every 10 ticks, what share should each thread get while all three are running?

## Part 2 — Turn aging off

Make `age_ready_threads` do nothing and repeat the measurement.

* What order does the output come in now?
* What is this failure called, and which thread suffers it?
* The shell has priority 10. Why does it still respond?

Restore aging before continuing.

## Part 3 — Implement a policy of your own

Choose one:

**A. Pure round-robin.** Ignore priorities: every runnable thread gets one time slice in turn. The idle thread must still run only when nothing else can.

**B. Lottery scheduling.** Give each thread tickets equal to its priority plus one, and pick the next thread by drawing a ticket. You need a random number generator; a 16-bit linear congruential generator is enough. Remember that 32-bit multiplication is slow on an 8086.

**C. Shorter slices for higher priorities.** Keep strict priorities and aging, but make the time slice depend on priority, so interactive threads are switched in quickly and often.

Keep your change inside `kernel/scheduler.c` and `kernel/scheduler.h`.

## Check

```sh
make test
```

Expect the check named "threads interleave (preemption + aging)" to keep passing for all three policies. If it fails, your policy lets one demo thread finish before another has started. The other checks should pass too; if a sleep-accuracy check fails, work out which thread was late and why.

Then repeat the Part 1 measurement and compare the shares with the baseline.

## Questions

1. `select_next_thread` scans starting *after* the current thread. What would change if it always scanned from thread 1?
2. Why does `sched_switch` reset `eff_priority` to the base priority when a thread is chosen?
3. The telemetry thread has priority 12, above the shell. What goes wrong on the dashboard if you give it priority 1 and then start three demo threads? (Try it, and watch the "Dropped" counter.)
4. For your policy: can a thread starve? Argue it, then try to construct a case.

## Going further

Use the dashboard's scheduler view (`./phoenix.sh`) to watch your policy live, and `bench` to see whether it changed the cost of a context switch.
