# Phoenix-8086 Visual Showcase Strategy

## Purpose

Phoenix-8086 is not only a systems project; it is a demonstration project. The kernel can be technically correct and still fail to impress if the audience cannot see what the machine is doing. This document defines the master visualization idea for the project so the implementation, presentation, and deployment story all reinforce one another.

The goal is to make the invisible visible: boot flow, memory layout, interrupt handling, scheduling, thread state, and fault conditions should all be observable in a polished presentation layer.

## Core Thesis

The project should be presented as a two-part system:

1. A faithful bare-metal Phoenix-8086 kernel running in an emulator.
2. A companion visualization surface that receives telemetry and turns kernel state into a live dashboard.

That separation matters. The kernel stays authentic to 8086 constraints, while the viewer gets an understandable, recruiter-friendly display of what is happening internally.

## What People Should Be Able To See

The visualization system should answer the questions people naturally ask during a demo:

* What stage is the boot process in?
* Where did the kernel get loaded in memory?
* Which thread is running right now?
* What changed during the context switch?
* Which interrupts are firing, and how often?
* How much stack or memory is each thread consuming?
* What happens when something goes wrong?

If a viewer can answer those questions by looking at the dashboard, the project is doing its job.

## Presentation Architecture

The master demo should be organized into three layers.

```text
Phoenix-8086

┌──────────────────────────────┐
│ Bare-Metal Kernel            │
└──────────────────────────────┘
              │
              ▼
┌──────────────────────────────┐
│ Telemetry Interface          │
└──────────────────────────────┘
              │
              ▼
┌──────────────────────────────┐
│ Visual Dashboard             │
└──────────────────────────────┘
```

### Layer 1: Kernel

The kernel produces the ground truth: boot state, thread state, interrupt counts, register snapshots, memory usage, and fault information.

### Layer 2: Telemetry

The telemetry layer turns internal state into small, structured messages that are easy to render and easy to replay.

### Layer 3: Dashboard

The dashboard renders the data as a live systems monitor, with animations and panels that explain the current state at a glance.

## Visual Design Principles

* Show state transitions, not just final state.
* Make each subsystem visible in its own panel.
* Keep the boot sequence narrative and sequential.
* Use motion only where it explains a systems event.
* Prefer dense, information-rich layouts over decorative but empty UI.
* Use consistent labels, colors, and status semantics across the whole demo.

## Primary Visual Modules

### 1. Boot Timeline

The boot process should look like a story, not a jump cut.

The dashboard should show steps such as:

* BIOS loaded
* Stage 1 bootloader active
* Stack initialized
* Stage 2 being read from disk
* Kernel image loaded
* Interrupt table installed
* Scheduler initialized
* Kernel entry reached

This gives the audience a clear sense of progression and makes the boot architecture feel intentional.

### 2. Memory Map Viewer

Memory is one of the hardest concepts to explain in systems projects, so it should always be visible.

The memory map should show at least:

* Interrupt Vector Table
* BIOS data area
* Kernel image
* TCB region
* Thread stacks
* Heap or reserved workspace

The map should highlight regions when they are read, written, or switched in the context of a thread operation.

### 3. Scheduler Timeline

This is the most important visual in the entire project.

The scheduler view should make preemption obvious by showing:

* The current running thread
* The ready queue
* The order of thread rotation
* The amount of CPU time each thread has consumed
* The effect of priority on scheduling decisions

If the audience can watch thread A yield to thread B and then return later, the scheduler becomes self-explanatory.

### 4. Thread Inspector

Every thread should have an expandable inspector panel that exposes:

* Thread ID
* Name
* State
* Priority
* Register snapshot
* Stack usage
* Last scheduled time
* Blocking reason, if any

This turns the TCB concept from abstract data structure into something tangible.

### 5. Live Register Viewer

The dashboard should display CPU registers at the point of context switch.

Important registers to show:

* AX, BX, CX, DX
* SP, BP
* CS, IP
* FLAGS

The point is not to overwhelm the audience with raw numbers. The point is to prove that the thread really was saved and restored correctly.

### 6. Interrupt Visualizer

Interrupts should be logged as visible events, not hidden background noise.

The visualizer should distinguish:

* Timer interrupts
* Keyboard interrupts
* Software interrupts
* Context switch events
* Fault or panic events

It should also show interrupt frequency over time so the viewer can see that the system is alive and responsive.

### 7. Context Switch Animation

Context switching is one of the signature events of the project and should be animated explicitly.

Suggested sequence:

* Save registers
* Store current TCB
* Choose next READY thread
* Restore registers
* Return through IRET
* Begin executing the new thread

The animation should be short, clear, and synchronized with the actual state change.

### 8. Console and Shell View

The shell is the audience’s control surface.

It should support commands that open specific visual panels or trigger demonstrations, such as:

* threads
* memory
* interrupts
* registers
* uptime
* benchmark
* panic
* reboot

This gives the demo a guided, interactive format instead of a passive one.

### 9. Failure and Recovery View

Good systems demos include controlled failure.

The dashboard should show at least one deliberate failure path, such as:

* stack overflow detection
* invalid state transition
* kernel panic
* thread termination due to a fault

That makes the project feel engineered rather than merely polished.

## Telemetry Model

The kernel should emit compact state updates that the dashboard can parse without needing to understand assembly internals.

Useful telemetry categories:

* Boot stage changes
* Thread creation and destruction
* Thread state changes
* Context switch events
* Interrupt counters
* Memory allocation summaries
* Register snapshots
* Fault reports

Each telemetry message should be structured enough to render consistently and small enough to be emitted frequently.

## Dashboard Layout

The dashboard should feel like a miniature operating-system monitor rather than a generic project page.

Recommended layout:

* Top bar: boot status, uptime, interrupt rate, context-switch count
* Left panel: thread list and scheduler queue
* Center panel: active visualization, such as boot timeline or context switch animation
* Right panel: registers, memory map, or thread inspector
* Bottom panel: interrupt log and event stream

This layout keeps the important systems information visible without forcing the user to switch modes constantly.

## Visual Language

The style should communicate precision, not gimmickry.

Use:

* restrained but high-contrast colors
* clear status states such as running, ready, blocked, and faulted
* monospaced or technical typography for machine data
* subtle motion for transitions and selection changes
* compact labels that match the terminology in the master project brief

Avoid:

* decorative animation with no informational purpose
* generic academic-slide aesthetics
* UI clutter that hides the state of the system

## Demonstration Flow

The final presentation should follow a predictable sequence:

1. Boot animation
2. Memory layout reveal
3. Interrupt setup visualization
4. Scheduler activation
5. Live thread switching
6. Register and TCB inspection
7. Keyboard or shell interaction
8. Controlled failure or panic demo
9. Performance summary

This gives the audience a narrative arc: setup, operation, inspection, and stress test.

## Deployment Story

The best deployment setup is a split architecture:

* Emulator window for the actual Phoenix-8086 kernel
* Companion dashboard for visual telemetry and presentation
* Presenter or external display for the audience view

That arrangement keeps the kernel faithful to its environment while letting the project be shown clearly during demos, interviews, or grading sessions.

## Scope for the Visual System

### Must Have

* Boot timeline
* Memory map
* Scheduler queue
* Thread inspector
* Register viewer
* Interrupt log
* Basic shell commands

### Strongly Recommended

* Context switch animation
* Performance counters
* Controlled fault visualization
* Stack usage indicators
* Thread CPU time tracking

### Stretch Ideas

* Interactive replay of events
* Historical charts for interrupts and scheduling
* Color-coded dependency graph of kernel subsystems
* Side-by-side comparison of threads over time

## Alignment With Project Details

This visualization strategy maps directly to the master project brief:

* Boot timeline maps to the two-stage boot flow.
* Memory map maps to kernel regions, stacks, and reserved space.
* Scheduler timeline maps to the Round-Robin and priority-aware thread model.
* Thread inspector maps to the TCB structure.
* Register viewer maps to context save and restore.
* Interrupt visualizer maps to the interrupt management layer.
* Shell view maps to the software interrupt and diagnostic interface.

If the project is implemented correctly, the dashboard should become a live explanation of the architecture rather than a separate decorative layer.

## Final Goal

The finished project should feel less like a hidden assembly assignment and more like a serious operating-system exhibit. A recruiter or professor should be able to boot the system, watch the kernel come alive, observe scheduling and interrupt behavior in real time, and understand the engineering quality without reading every line of code.