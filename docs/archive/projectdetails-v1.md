
# Phoenix-8086 Master Project Details

## Project Summary

Phoenix-8086 is a bare-metal 8086 systems project focused on building a small but technically rigorous operating environment from first principles. The goal is to design a two-stage boot path, load a kernel without relying on DOS services, and implement a preemptive scheduling model with interrupt-driven device handling.

This document is the authoritative project brief for the team. It consolidates the original scattered notes into one structured specification so the work can be divided cleanly and implemented consistently.

## Project Goals

* Boot directly from a floppy image or emulator-backed boot medium.
* Load a second-stage loader and kernel into memory under BIOS control.
* Initialize a safe execution environment, including stack, segments, and interrupt state.
* Provide preemptive multitasking through timer-driven context switching.
* Support basic kernel services such as console output, keyboard input, and software interrupts.
* Present the system as a coherent educational capstone with clear subsystems and measurable milestones.

## Design Principles

* Keep the implementation faithful to 8086-era constraints.
* Prefer simple, inspectable code paths over hidden abstractions.
* Separate boot, kernel, scheduler, and device logic into distinct phases.
* Make the baseline system complete before adding stretch features.
* Document assumptions and constraints so the team can work independently without conflicting implementations.

## Target Environment

* CPU model: Intel 8086-compatible execution model.
* Boot environment: BIOS-based boot flow in an emulator such as emu8086.
* Memory model: 16-bit segmented real mode.
* Storage model: floppy-style boot medium or equivalent emulator image.
* Output model: text-mode console for early kernel interaction.

## System Architecture

### Stage 1 Bootloader

The first-stage bootloader is responsible for the minimal work required to start the system.

Responsibilities:

* Fit inside one boot sector.
* Use `ORG 7C00h` and correct boot-sector layout assumptions.
* Set up a working stack and segment registers.
* Detect and preserve the boot drive number.
* Load Stage 2 from disk using BIOS disk services.
* Transfer control to Stage 2 with a far jump or equivalent handoff.

### Stage 2 Loader

The second-stage loader expands the boot environment and prepares the kernel.

Responsibilities:

* Display boot status messages.
* Load the kernel image into memory.
* Prepare kernel entry conditions.
* Establish any required memory map or hardware discovery data available in the emulator.
* Hand off execution to the kernel entry point.

### Kernel Core

The kernel provides the runtime environment for scheduling, interrupts, and basic services.

Responsibilities:

* Install interrupt handlers.
* Initialize scheduler data structures.
* Create and manage thread contexts.
* Provide console and keyboard services.
* Offer a small software interrupt interface for system calls.

## Core Subsystems

### 1. Boot and Disk Loading

The boot path must load the system in stages rather than trying to do everything in the first sector.

Baseline requirements:

* Read Stage 2 using BIOS INT 13h.
* Use a deterministic memory destination for loader and kernel images.
* Validate that the kernel can be transferred into executable memory before control is handed off.

### 2. Thread Model and Task Control Blocks

Threads are represented by Task Control Blocks (TCBs) stored in kernel-managed memory.

Minimum TCB fields:

| Offset | Field | Size | Purpose |
| --- | --- | --- | --- |
| +0x00 | SP | 16-bit | Current stack pointer |
| +0x02 | SS | 16-bit | Stack segment |
| +0x04 | IP | 16-bit | Resume address |
| +0x06 | CS | 16-bit | Code segment |
| +0x08 | FLAGS | 16-bit | Processor flags |
| +0x0A | General-purpose registers | 14 bytes | AX, BX, CX, DX, SI, DI, BP |
| +0x18 | Thread state | 8-bit | READY, RUNNING, BLOCKED |
| +0x19 | Priority | 8-bit | Scheduling priority |

Thread lifecycle operations:

* CreateThread
* DestroyThread
* SuspendThread
* ResumeThread
* YieldThread

### 3. Preemptive Scheduler

The scheduler should be timer-driven and capable of preempting the currently running thread.

Baseline requirements:

* Use a Round-Robin core.
* Support priority-aware selection among READY threads.
* Save and restore full thread context on each switch.
* Keep interrupt handler overhead low enough to preserve timer stability.

Context-switch requirements:

* Save the interrupted thread state to its TCB.
* Select the next eligible READY thread.
* Restore its context from the TCB.
* Return through `IRET` only after the stack has been restored correctly.

### 4. Interrupt Management

The kernel must own interrupt installation and dispatch.

Baseline interrupts:

* IRQ0 timer tick
* Keyboard interrupt
* INT 80h software interrupt for system calls

Responsibilities:

* Install handlers in the interrupt vector table.
* Acknowledge interrupts correctly with End of Interrupt signaling where required.
* Keep interrupt handlers small and predictable.
* Separate interrupt dispatch from higher-level kernel logic.

### 5. Console Driver

The kernel should expose a text-mode console for debugging and interaction.

Baseline functions:

* PrintChar
* PrintString
* ClearScreen
* ScrollScreen
* MoveCursor
* SetColor

Implementation target:

* Direct writes to VGA text memory at `B800:0000` or emulator-equivalent text memory.

### 6. Keyboard Driver

The system should read keyboard input through direct hardware handling rather than relying on BIOS keyboard services.

Baseline requirements:

* Capture scan codes from the keyboard controller.
* Translate a minimal set of scan codes to printable characters.
* Route keyboard events into the console or kernel input layer.

### 7. Software Interrupt System Calls

The kernel should expose a small software interrupt interface for user-style requests.

Suggested system call classes:

* Character output
* Thread creation
* Thread exit
* Yield or sleep
* Tick count query

### 8. Memory Management

The baseline project does not need a full virtual memory system, but it should manage memory intentionally.

Baseline requirements:

* Reserve memory regions for kernel code, stacks, TCBs, and buffers.
* Keep loader and kernel allocations separate.
* Avoid overlapping thread stacks and kernel data.

## Scope Boundaries

### In Scope for the Baseline

* Two-stage boot flow
* Kernel loading and handoff
* Preemptive scheduling
* Thread context management
* Timer and keyboard interrupts
* Console output
* Minimal software interrupts

### Stretch Goals

* Dynamic thread priorities with aging or starvation control
* IPC via mailboxes or message queues
* Richer memory management beyond static reservation
* More complete system call coverage
* Compatibility layer for additional BIOS-like services
* Interactive shell with command parsing and utilities

## Milestones

### Milestone 1: Boot Path

Deliverable:

* Stage 1 boots and loads Stage 2.
* Stage 2 loads the kernel and reaches kernel entry reliably.

### Milestone 2: Kernel Bring-Up

Deliverable:

* Kernel initializes segments, stack, and console.
* Basic text output works from kernel code.

### Milestone 3: Interrupts and Scheduling

Deliverable:

* Timer interrupt fires.
* Scheduler can switch between at least two threads.
* Context save and restore are stable.

### Milestone 4: Input and Services

Deliverable:

* Keyboard input is available.
* INT 80h or equivalent syscall entry is functional.
* A minimal shell or command loop can run.

## Success Criteria

The project is complete when the following are true:

* The system boots without DOS assistance.
* The kernel loads and transfers control cleanly.
* At least two threads can run under preemptive scheduling.
* Interrupt handlers are stable and properly acknowledged.
* Console and keyboard interaction work inside the kernel.
* The codebase is documented well enough for teammates to continue implementation independently.

## Implementation Notes

* Keep bootloader code size tightly controlled.
* Use clear labels and data layouts so TCB offsets remain stable.
* Treat interrupt context saving as a critical path.
* Prefer small, testable increments over adding advanced features before the core boot chain works.
* When adding stretch goals, document the dependency chain so they do not destabilize the baseline kernel.

## Project Positioning

Phoenix-8086 is intended to demonstrate disciplined low-level systems design rather than feature breadth alone. The project should read as a serious educational kernel exercise: technically ambitious, structurally clean, and feasible enough that the team can complete the baseline while still leaving room for deeper stretch work.
---
---

# Project Title

**Phoenix-8086: A Bare-Metal Preemptive Microkernel with Dynamic Task Management and Interrupt-Driven Device Services**

---

# Improved Project Overview

Design and implement a fully functional bare-metal operating environment for the Intel 8086 processor without relying on DOS or any external operating system.

The system will consist of:

* Stage-1 Bootloader
* Stage-2 Kernel Loader
* Preemptive Microkernel
* Interrupt Management Layer
* Dynamic Thread Scheduler
* Basic Device Drivers
* Inter-Process Communication (IPC)
* Memory Manager
* Interactive Kernel Shell

The system should boot directly from a floppy image, initialize hardware, configure interrupts, create multiple execution contexts, and support concurrent execution of user threads.

---

# Major Improvements

## 1. Two-Stage Bootloader

Instead of loading the entire kernel directly from sector 2, divide the boot process.

```
BIOS
   │
   ▼
Stage 1 Bootloader (512 bytes)
   │
   ▼
Stage 2 Loader
   │
   ▼
Kernel
```

Stage 1

* Fits inside one boot sector
* Initializes stack
* Detects boot drive
* Reads Stage 2

Stage 2

* Displays boot messages
* Detects RAM size
* Loads kernel
* Builds memory map
* Transfers control

This mimics real operating systems.

---

# 2. Dynamic Thread Creation

Instead of statically creating threads,

implement

```
CreateThread()

DestroyThread()

SuspendThread()

ResumeThread()
```

Each thread receives

* private stack
* initial register context
* state
* priority

Instead of

```
Thread1

Thread2

Thread3
```

allow

```
Thread A created

Thread B destroyed

Thread C created later
```

This is much closer to real kernels.

---

# 3. Priority-Based Round Robin

Instead of

```
1
2
3
1
2
3
```

implement

```
Priority 3

Priority 2

Priority 1
```

Scheduling example

```
A
A
B
A
C
A
B
```

This introduces

* starvation prevention
* time slice accounting
* scheduling algorithms

---

# 4. Full Interrupt Management Layer

Instead of only replacing INT 1Ch,

create an interrupt subsystem.

Example

```
IRQ0 Timer

IRQ1 Keyboard

INT 80h System Call

INT 21h Compatibility Layer
```

Maintain an interrupt vector table inside the kernel.

---

# 5. Keyboard Driver

Instead of BIOS keyboard services,

directly read the keyboard controller.

Ports

```
60h

64h
```

Convert scan codes

```
1E

↓

'A'
```

Now your kernel has a real device driver.

---

# 6. Console Driver

Create a VGA text-mode console.

Functions

```
PrintChar()

PrintString()

MoveCursor()

ScrollScreen()

SetColor()

ClearScreen()
```

Write directly to

```
B800:0000
```

instead of using BIOS.

---

# 7. Software Interrupt System Calls

Create

```
INT 80h
```

similar to Linux.

Applications call

```
mov ah,01
int 80h
```

Kernel performs

```
Print Character

Create Thread

Exit Thread

Sleep

Yield

Get Tick Count
```

This demonstrates the transition from user requests to kernel services.

---

# 8. Inter-Process Communication (IPC)

Implement message passing.

Example

```
Producer

↓

Mailbox

↓

Consumer
```

Functions

```
Send()

Receive()

Broadcast()
```

Now threads communicate without shared variables.

---

# 9. Synchronization Primitives

Implement

```
Semaphore

Mutex

Spinlock
```

Example

```
Acquire()

Release()
```

This teaches concurrent programming.

---

# 10. Sleep Queue

Instead of busy waiting,

allow

```
Sleep(50 ticks)
```

Scheduler moves thread

```
READY

↓

SLEEPING

↓

READY
```

when timer expires.

---

# 11. Dynamic Memory Manager

Even without paging,

implement

```
Heap

Free List

Memory Blocks
```

Functions

```
malloc()

free()
```

based on first-fit allocation.

---

# 12. Software Loader

Allow loading another program stored on disk.

Example

```
Kernel

↓

Load APP.BIN

↓

Create Thread

↓

Execute
```

Now the kernel becomes an actual operating system.

---

# 13. Kernel Command Shell

Create a command-line shell.

Commands

```
help

threads

memory

ticks

clear

reboot

uptime

kill

create

ps
```

This makes demonstrations much more engaging.

---

# 14. Runtime Statistics

Maintain kernel statistics.

Example

```
CPU Usage

Context Switches

Timer Interrupts

Memory Usage

Stack Usage

Ticks

Idle Time
```

Display them live.

---

# 15. Debug Mode

Implement a debug monitor.

```
Registers

Memory Dump

Stack Dump

Thread List

Interrupt Count

Scheduler Queue
```

Very impressive during demonstrations.

---

# 16. Stack Overflow Detection

Allocate a guard value.

```
AAAAh
```

Whenever a thread is switched,

verify

```
AAAAh
```

still exists.

If not

```
Stack Overflow

Terminate Thread
```

---

# 17. Idle Thread

Always maintain

```
Thread 0

Idle
```

When nothing is runnable,

CPU executes

```
HLT
```

until next interrupt.

Exactly how real kernels behave.

---

# 18. Kernel Panic System

Instead of crashing,

display

```
KERNEL PANIC

Reason:

Stack Corruption

Current Thread

Register Dump

CS

IP

FLAGS

SP
```

before halting.

---

# 19. Memory Map

Reserve memory.

```
00000-003FF

Interrupt Vector Table

00400-004FF

BIOS Data Area

00500-07BFF

Kernel Stack

07C00-07DFF

Bootloader

08000-0FFFF

Kernel

10000-2FFFF

Thread Stacks

30000-4FFFF

Heap
```

This demonstrates thoughtful system design.

---

# 20. Performance Benchmark

Measure

```
Context Switch Time

Interrupt Latency

Scheduling Overhead

Boot Time

Memory Allocation Time
```

using timer ticks.

---

# Suggested Final Architecture

```
                 BIOS
                  │
                  ▼
         Stage 1 Bootloader
                  │
                  ▼
         Stage 2 Kernel Loader
                  │
                  ▼
          Hardware Initialization
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
 Interrupt Manager     Memory Manager
        │                   │
        └─────────┬─────────┘
                  ▼
        Preemptive Scheduler
                  │
        ┌─────────┼─────────┐
        ▼         ▼         ▼
     Thread A  Thread B  Thread C
        │         │         │
        └─────────┼─────────┘
                  ▼
          System Call Layer
                  │
        ┌─────────┼─────────┐
        ▼         ▼         ▼
     Console   Keyboard   Timer
        │
        ▼
     Kernel Shell
```

## What would make this truly exceptional?

The biggest differentiator is **design quality**, not just feature count. A kernel that cleanly separates the **Bootloader**, **Hardware Abstraction Layer (HAL)**, **Interrupt Manager**, **Scheduler**, **Memory Manager**, **Device Drivers**, and **System Call Interface** into distinct modules demonstrates engineering discipline that resembles real operating systems. Adding a lightweight debugging monitor, runtime diagnostics, and a small shell turns the project from a proof of concept into a miniature operating system.

A project with these characteristics would be an excellent undergraduate capstone, showcasing expertise in x86 assembly, processor architecture, interrupt handling, low-level memory management, operating system principles, and systems software design. It would also provide a strong foundation for later work on protected mode, paging, and more advanced kernels.
