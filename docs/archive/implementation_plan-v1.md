# Phoenix-8086 — Complete Implementation Plan & Progress Tracker

## Project Summary

Build **Phoenix-8086**, a bare-metal preemptive microkernel for the Intel 8086 processor, together with a **companion web-based visual dashboard** that displays live kernel telemetry. The kernel itself is written entirely in **x86 Assembly (NASM)** and **C** (with a small amount of C++ for the telemetry bridge). The dashboard is a standalone web application built with HTML, CSS, and vanilla JavaScript.

> [!IMPORTANT]
> This plan doubles as the **phase-based to-do list**. Each checkbox tracks whether the work item is not started `[ ]`, in progress `[/]`, or completed `[x]`. Update this document as work proceeds.

---

## Technology Stack

| Layer | Language / Tool | Rationale |
|---|---|---|
| Stage 1 Bootloader | **NASM x86 Assembly** | Must fit in 512 bytes; no room for anything but hand-tuned asm |
| Stage 2 Loader | **NASM x86 Assembly** | Direct BIOS calls, segment setup — assembly is the only practical choice |
| Kernel Core | **NASM x86 Assembly + C (gcc cross-compiler, i686-elf target)** | Critical paths (ISRs, context switch) stay in asm; higher-level logic (scheduler, memory manager, shell) written in C for readability |
| Device Drivers | **C + inline asm** | Port I/O wrappers in asm; driver logic in C |
| Telemetry Bridge | **C++ (minimal)** | Serializes kernel state into structured telemetry packets over a virtual serial port; uses lightweight C++ for string formatting |
| Build System | **Makefile + NASM + i686-elf-gcc + ld** | Standard bare-metal cross-compilation toolchain |
| Disk Image | **dd / mkfs utilities** | Produces a raw floppy image bootable in QEMU or emu8086 |
| Visual Dashboard | **HTML + CSS + Vanilla JavaScript** | Receives telemetry via WebSocket (from a small Python or C serial-to-WS bridge) and renders the live dashboard |
| Emulator | **QEMU (primary) / emu8086 (secondary)** | QEMU supports serial redirection for telemetry; emu8086 for classroom demos |

---

## Directory Structure (Planned)

```
visage/
├── boot/
│   ├── stage1.asm            # First-stage bootloader (512 bytes)
│   └── stage2.asm            # Second-stage loader
├── kernel/
│   ├── entry.asm             # Kernel entry point (asm)
│   ├── isr.asm               # Interrupt service routines (asm)
│   ├── context_switch.asm    # Context save/restore (asm)
│   ├── kernel.h              # Kernel-wide headers
│   ├── kernel_main.c         # Kernel C entry
│   ├── scheduler.c           # Round-robin + priority scheduler
│   ├── scheduler.h
│   ├── thread.c              # Thread lifecycle (create/destroy/suspend/resume)
│   ├── thread.h
│   ├── tcb.h                 # Task Control Block structure
│   ├── interrupts.c          # Interrupt manager
│   ├── interrupts.h
│   ├── memory.c              # Static memory manager + heap
│   ├── memory.h
│   ├── console.c             # VGA text-mode console driver
│   ├── console.h
│   ├── keyboard.c            # Keyboard driver (port 60h/64h)
│   ├── keyboard.h
│   ├── syscall.c             # INT 80h system call dispatcher
│   ├── syscall.h
│   ├── ipc.c                 # Mailbox-based IPC
│   ├── ipc.h
│   ├── sync.c                # Semaphore / mutex / spinlock
│   ├── sync.h
│   ├── shell.c               # Interactive kernel shell
│   ├── shell.h
│   ├── panic.c               # Kernel panic handler
│   ├── panic.h
│   ├── debug.c               # Debug monitor
│   ├── debug.h
│   ├── stats.c               # Runtime statistics
│   ├── stats.h
│   ├── idle.c                # Idle thread (HLT loop)
│   └── telemetry.cpp         # Telemetry serializer (C++)
├── include/
│   └── types.h               # Shared type definitions
├── linker/
│   └── kernel.ld             # Linker script for kernel layout
├── dashboard/
│   ├── index.html            # Visual dashboard entry
│   ├── styles.css            # Dashboard styling
│   ├── app.js                # Main dashboard JS application
│   ├── modules/
│   │   ├── boot_timeline.js  # Boot timeline panel
│   │   ├── memory_map.js     # Memory map viewer
│   │   ├── scheduler_view.js # Scheduler queue visualization
│   │   ├── thread_inspector.js # Thread detail inspector
│   │   ├── register_viewer.js  # Live register viewer
│   │   ├── interrupt_viz.js  # Interrupt visualizer
│   │   ├── context_switch.js # Context switch animation
│   │   ├── console_view.js   # Console / shell mirror
│   │   └── fault_view.js     # Failure & recovery panel
│   └── assets/
│       └── fonts/            # Monospace / technical fonts
├── bridge/
│   └── serial_ws_bridge.py   # Serial → WebSocket relay
├── tools/
│   ├── build.sh              # Master build script
│   └── run.sh                # Launch QEMU + bridge + dashboard
├── Makefile
├── projectdetails.md         # (existing) Master project brief
├── visual.md                 # (existing) Visualization strategy
└── implementation_plan.md    # This file
```

---

## Phase 1 — Toolchain & Project Skeleton

> **Goal**: Establish a working cross-compilation pipeline and project layout so every subsequent phase can build and test immediately.

- [ ] Install / verify NASM assembler
- [ ] Install / verify i686-elf cross-compiler (gcc + binutils)
- [ ] Install / verify QEMU (qemu-system-i386)
- [ ] Create the directory structure listed above
- [ ] Write the master `Makefile` with targets: `boot`, `kernel`, `image`, `run`, `dashboard`, `clean`
- [ ] Write `tools/build.sh` — orchestrates full build
- [ ] Write `tools/run.sh` — launches QEMU with serial redirect + bridge + dashboard
- [ ] Write `linker/kernel.ld` — linker script placing kernel at `0x8000`, stacks at `0x10000`, heap at `0x30000`
- [ ] Write `include/types.h` — `uint8_t`, `uint16_t`, `int16_t`, `bool`, `NULL`
- [ ] Verify: `make clean && make image && make run` boots into QEMU (can show black screen at this point)

---

## Phase 2 — Stage 1 Bootloader

> **Goal**: A 512-byte boot sector that sets up segments, detects the boot drive, loads Stage 2 from disk, and jumps to it.

- [ ] Write `boot/stage1.asm`
  - [ ] `ORG 7C00h`, `BITS 16`
  - [ ] Zero segment registers (DS, ES, SS)
  - [ ] Set up stack at `0x7C00` (grows downward)
  - [ ] Save boot drive number from DL
  - [ ] Use INT 13h/AH=02h to read Stage 2 sectors into `0x7E00`
  - [ ] Far jump to Stage 2 entry
  - [ ] Pad to 510 bytes + `0xAA55` signature
- [ ] Verify: QEMU boots and reaches Stage 2 (print a character from Stage 2 as proof)

---

## Phase 3 — Stage 2 Loader

> **Goal**: Display boot messages, detect available memory, load the kernel binary, and hand off to kernel entry.

- [ ] Write `boot/stage2.asm`
  - [ ] Display "Stage 2 active" via BIOS INT 10h
  - [ ] Detect memory size (INT 12h or INT 15h/AH=88h)
  - [ ] Read kernel sectors from disk into `0x8000` using INT 13h
  - [ ] Display "Kernel loaded" confirmation
  - [ ] Set up segment registers for kernel entry
  - [ ] Far jump to kernel entry point at `0x8000`
- [ ] Verify: Boot chain reaches kernel entry; a character or string printed from kernel entry confirms handoff

---

## Phase 4 — Kernel Entry & Console Driver

> **Goal**: The kernel starts executing, initializes BSS, sets up its own stack, and has a working VGA text-mode console.

- [ ] Write `kernel/entry.asm`
  - [ ] Kernel entry label at `0x8000`
  - [ ] Set DS, ES, SS to kernel data segment
  - [ ] Set SP to kernel stack top
  - [ ] Zero the BSS section
  - [ ] Call `kernel_main` (C function)
- [ ] Write `kernel/console.h` / `kernel/console.c`
  - [ ] Direct VGA write at `0xB8000` (physical) / `B800:0000` (real mode)
  - [ ] `void con_clear(void)`
  - [ ] `void con_putchar(char c, uint8_t color)`
  - [ ] `void con_print(const char *str)`
  - [ ] `void con_scroll(void)`
  - [ ] `void con_set_cursor(uint8_t row, uint8_t col)`
  - [ ] `void con_set_color(uint8_t fg, uint8_t bg)`
- [ ] Write `kernel/kernel_main.c`
  - [ ] Call `con_clear()`
  - [ ] Print "Phoenix-8086 Kernel v0.1"
  - [ ] Halt loop (`for(;;) asm("hlt");`)
- [ ] Verify: Full boot chain shows the welcome message on screen

---

## Phase 5 — Interrupt Management

> **Goal**: Install custom interrupt handlers for timer (IRQ0), keyboard (IRQ1), and software interrupt (INT 80h).

- [ ] Write `kernel/interrupts.h` / `kernel/interrupts.c`
  - [ ] `void idt_install(uint8_t vector, void (*handler)(void))`
  - [ ] `void irq_init(void)` — remap PIC if needed, install IRQ0 and IRQ1
  - [ ] `void irq_eoi(uint8_t irq)` — send End-of-Interrupt to PIC
  - [ ] Global interrupt counter variables
- [ ] Write `kernel/isr.asm`
  - [ ] Timer ISR stub: save regs → call C handler → EOI → `IRET`
  - [ ] Keyboard ISR stub: save regs → call C handler → EOI → `IRET`
  - [ ] INT 80h stub: save regs → call syscall dispatcher → `IRET`
  - [ ] Common register save/restore macros
- [ ] Wire up in `kernel_main.c`: call `irq_init()` after console init
- [ ] Verify: Timer interrupt fires (increment a global counter, display it); keyboard interrupt captures a keypress

---

## Phase 6 — Keyboard Driver

> **Goal**: Read raw scan codes from port `0x60`, translate to ASCII, and buffer input.

- [ ] Write `kernel/keyboard.h` / `kernel/keyboard.c`
  - [ ] `void kb_init(void)` — install IRQ1 handler
  - [ ] `void kb_isr_handler(void)` — read port 60h, translate scan code, push to ring buffer
  - [ ] `char kb_getchar(void)` — blocking read from buffer
  - [ ] `bool kb_haschar(void)` — non-blocking check
  - [ ] Scan-code-to-ASCII table (minimal: A–Z, 0–9, space, enter, backspace)
- [ ] Verify: Typing on keyboard echoes characters to console

---

## Phase 7 — Thread Model & TCB

> **Goal**: Define the Task Control Block, implement thread creation and destruction.

- [ ] Write `kernel/tcb.h`
  - [ ] TCB struct matching the spec (SP, SS, IP, CS, FLAGS, general regs, state, priority)
  - [ ] Thread states: `READY`, `RUNNING`, `BLOCKED`, `SLEEPING`, `TERMINATED`
  - [ ] TCB array (static, e.g., `MAX_THREADS = 8`)
- [ ] Write `kernel/thread.h` / `kernel/thread.c`
  - [ ] `int thread_create(void (*entry)(void), uint8_t priority)` — allocate TCB, set up initial stack frame
  - [ ] `void thread_destroy(int tid)`
  - [ ] `void thread_suspend(int tid)`
  - [ ] `void thread_resume(int tid)`
  - [ ] `void thread_yield(void)`
  - [ ] Per-thread stack allocation from the `0x10000–0x2FFFF` region
- [ ] Write `kernel/idle.c`
  - [ ] Idle thread function: `for(;;) asm("hlt");`
  - [ ] Always thread ID 0, lowest priority
- [ ] Verify: `thread_create` allocates a TCB; idle thread is created at boot

---

## Phase 8 — Preemptive Scheduler & Context Switch

> **Goal**: Timer-driven preemptive Round-Robin scheduler with priority support and correct context save/restore.

- [ ] Write `kernel/context_switch.asm`
  - [ ] `context_save`: push all general-purpose registers + FLAGS onto current thread's stack, save SP/SS to TCB
  - [ ] `context_restore`: load SP/SS from next thread's TCB, pop all registers + FLAGS
  - [ ] Critical: return via `IRET` only after stack is fully restored
- [ ] Write `kernel/scheduler.h` / `kernel/scheduler.c`
  - [ ] Ready queue (circular linked list or array scan)
  - [ ] `void sched_init(void)` — create idle thread, set current = idle
  - [ ] `void sched_schedule(void)` — called from timer ISR; pick next READY thread (priority-aware RR)
  - [ ] `void sched_add(int tid)` / `void sched_remove(int tid)`
  - [ ] Priority-based selection: higher priority threads get more frequent scheduling
  - [ ] Starvation prevention: aging mechanism (increment effective priority of starved threads)
- [ ] Integrate into timer ISR: on each tick, call `sched_schedule()`
- [ ] Write two demo threads (e.g., print "A" and print "B" in loops with delays) to test preemption
- [ ] Verify: Two threads visibly alternate output; context switch does not corrupt registers

---

## Phase 9 — Software Interrupt System Calls (INT 80h)

> **Goal**: Expose kernel services through a software interrupt interface.

- [ ] Write `kernel/syscall.h` / `kernel/syscall.c`
  - [ ] Syscall dispatcher: read function number from AH
  - [ ] `AH=01h` — Print character (AL = char)
  - [ ] `AH=02h` — Create thread (BX = entry address, CL = priority) → returns TID in AX
  - [ ] `AH=03h` — Exit current thread
  - [ ] `AH=04h` — Yield
  - [ ] `AH=05h` — Sleep (CX = tick count)
  - [ ] `AH=06h` — Get tick count → returns in DX:AX
- [ ] Verify: A user-style thread uses INT 80h to print, yield, and exit correctly

---

## Phase 10 — Sleep Queue & Blocking

> **Goal**: Allow threads to sleep for a specified number of ticks and wake automatically.

- [ ] Extend `kernel/scheduler.c`
  - [ ] Sleep list: array of (tid, wake_tick) pairs
  - [ ] `void sched_sleep(int tid, uint16_t ticks)` — move thread to SLEEPING, record wake time
  - [ ] On each timer tick: scan sleep list, wake expired threads (set to READY)
- [ ] Verify: A thread sleeps for N ticks and resumes correctly

---

## Phase 11 — Memory Manager

> **Goal**: Static region reservation + a simple first-fit heap allocator.

- [ ] Write `kernel/memory.h` / `kernel/memory.c`
  - [ ] Memory map constants matching the spec (IVT, BIOS data, kernel, stacks, heap)
  - [ ] `void mem_init(void)` — initialize free list over the heap region `0x30000–0x4FFFF`
  - [ ] `void *kmalloc(uint16_t size)` — first-fit allocation
  - [ ] `void kfree(void *ptr)`
  - [ ] Free-list node: `{ uint16_t size; struct node *next; }`
- [ ] Stack overflow guard: write `0xAAAA` at bottom of each thread stack; check on context switch
- [ ] Verify: `kmalloc` / `kfree` cycle works; stack guard detects overflow

---

## Phase 12 — Synchronization Primitives

> **Goal**: Semaphore, mutex, and spinlock implementations.

- [ ] Write `kernel/sync.h` / `kernel/sync.c`
  - [ ] `typedef struct { int16_t count; int wait_queue[MAX_THREADS]; } semaphore_t;`
  - [ ] `void sem_init(semaphore_t *s, int16_t initial)`
  - [ ] `void sem_wait(semaphore_t *s)` — decrement; block if negative
  - [ ] `void sem_signal(semaphore_t *s)` — increment; wake one waiter
  - [ ] `typedef struct { int owner; bool locked; } mutex_t;`
  - [ ] `void mutex_lock(mutex_t *m)` / `void mutex_unlock(mutex_t *m)`
  - [ ] `void spinlock_acquire(volatile bool *lock)` / `void spinlock_release(volatile bool *lock)` — asm `lock` prefix where applicable
- [ ] Verify: Producer-consumer demo with a semaphore-guarded buffer

---

## Phase 13 — Inter-Process Communication (IPC)

> **Goal**: Mailbox-based message passing between threads.

- [ ] Write `kernel/ipc.h` / `kernel/ipc.c`
  - [ ] `typedef struct { uint16_t data[MAILBOX_SIZE]; int head, tail, count; } mailbox_t;`
  - [ ] `void mbox_init(mailbox_t *m)`
  - [ ] `void mbox_send(mailbox_t *m, uint16_t msg)` — block if full
  - [ ] `uint16_t mbox_recv(mailbox_t *m)` — block if empty
  - [ ] `void mbox_broadcast(mailbox_t *m, uint16_t msg)` — send to all receivers
- [ ] Verify: Two threads exchange messages through a mailbox

---

## Phase 14 — Kernel Shell

> **Goal**: An interactive command-line shell running as a thread, driving demos and diagnostics.

- [ ] Write `kernel/shell.h` / `kernel/shell.c`
  - [ ] Command input loop using `kb_getchar()`
  - [ ] Line buffer with backspace support
  - [ ] Command parser: tokenize by space, match first token
  - [ ] Commands:
    - [ ] `help` — list commands
    - [ ] `threads` / `ps` — show thread table (ID, state, priority)
    - [ ] `memory` — show memory map and heap stats
    - [ ] `ticks` — show tick counter
    - [ ] `uptime` — show uptime in seconds
    - [ ] `clear` — clear screen
    - [ ] `reboot` — jump to BIOS reset vector
    - [ ] `kill <tid>` — destroy a thread
    - [ ] `create` — create a demo thread
    - [ ] `panic` — trigger a deliberate kernel panic
    - [ ] `benchmark` — run context-switch benchmark
- [ ] Verify: Shell launches at boot; all commands produce correct output

---

## Phase 15 — Kernel Panic & Debug Monitor

> **Goal**: Graceful panic handling and a diagnostic debug monitor.

- [ ] Write `kernel/panic.h` / `kernel/panic.c`
  - [ ] `void kernel_panic(const char *reason)` — disable interrupts, dump registers, display red panic screen, halt
  - [ ] Register dump: CS, IP, FLAGS, SP, AX–DX, SI, DI, BP
  - [ ] Current thread info
- [ ] Write `kernel/debug.h` / `kernel/debug.c`
  - [ ] `void debug_dump_regs(void)`
  - [ ] `void debug_dump_mem(uint16_t seg, uint16_t off, uint16_t len)`
  - [ ] `void debug_dump_stack(int tid)`
  - [ ] `void debug_thread_list(void)`
  - [ ] `void debug_irq_counts(void)`
  - [ ] `void debug_sched_queue(void)`
- [ ] Write `kernel/stats.h` / `kernel/stats.c`
  - [ ] Track: context switches, timer interrupts, keyboard interrupts, idle ticks, per-thread CPU time
- [ ] Verify: `panic` command triggers panic screen; debug commands show correct data

---

## Phase 16 — Telemetry System

> **Goal**: Emit structured kernel telemetry over a virtual serial port so the dashboard can consume it.

- [ ] Write `kernel/telemetry.cpp`
  - [ ] Initialize serial port (COM1, 0x3F8) at 115200 baud
  - [ ] `void tel_emit(uint8_t type, const void *data, uint8_t len)` — write a framed telemetry packet
  - [ ] Telemetry types: `BOOT_STAGE`, `THREAD_EVENT`, `CONTEXT_SWITCH`, `IRQ_COUNTER`, `REGISTER_SNAPSHOT`, `MEMORY_SUMMARY`, `FAULT`
  - [ ] Packet format: `[START_BYTE][TYPE][LEN][DATA...][CHECKSUM]`
  - [ ] Hook telemetry calls into: boot stages, thread create/destroy, context switch, panic
- [ ] Write `bridge/serial_ws_bridge.py`
  - [ ] Read from QEMU serial pipe / PTY
  - [ ] Parse telemetry packets
  - [ ] Relay as JSON over WebSocket (port 9090)
- [ ] Verify: Telemetry packets appear in bridge output; WebSocket delivers JSON to a test page

---

## Phase 17 — Visual Dashboard (Web Application)

> **Goal**: A premium, dark-themed, real-time dashboard that renders kernel telemetry as described in `visual.md`.

### 17.1 — Dashboard Shell & Design System

- [ ] Write `dashboard/index.html` — semantic layout with panels
- [ ] Write `dashboard/styles.css` — full dark design system
  - [ ] Color palette: deep navy/charcoal backgrounds, electric blue/amber/green accents
  - [ ] Typography: monospace for data (JetBrains Mono or Fira Code), sans-serif for labels (Inter)
  - [ ] Panel component: glassmorphism card with subtle border glow
  - [ ] Status badges: running (green), ready (blue), blocked (amber), sleeping (purple), terminated (red)
  - [ ] Micro-animations: fade-in for new data, pulse for active thread, slide for transitions
- [ ] Write `dashboard/app.js` — WebSocket connection, message routing to modules

### 17.2 — Dashboard Panels

- [ ] `dashboard/modules/boot_timeline.js`
  - [ ] Vertical timeline with step labels and animated progression
  - [ ] Steps: BIOS → Stage 1 → Stack Init → Stage 2 Load → Kernel Load → IRQ Install → Scheduler Init → Kernel Ready
- [ ] `dashboard/modules/memory_map.js`
  - [ ] Stacked bar / block diagram of memory regions
  - [ ] Highlight active regions on access events
- [ ] `dashboard/modules/scheduler_view.js`
  - [ ] Horizontal Gantt-style timeline showing which thread runs in each time slice
  - [ ] Ready queue visualization as an ordered list
  - [ ] CPU time bar chart per thread
- [ ] `dashboard/modules/thread_inspector.js`
  - [ ] Expandable card per thread: ID, name, state, priority, registers, stack usage, last scheduled
- [ ] `dashboard/modules/register_viewer.js`
  - [ ] Grid of register values (AX–DX, SP, BP, CS, IP, FLAGS)
  - [ ] Highlight changes on context switch
- [ ] `dashboard/modules/interrupt_viz.js`
  - [ ] Event stream log (scrolling list of recent interrupts)
  - [ ] Interrupt frequency sparkline chart
  - [ ] Counters: timer, keyboard, syscall, context switch
- [ ] `dashboard/modules/context_switch.js`
  - [ ] Animated sequence: save → select → restore → IRET
  - [ ] Shows old and new thread with register values
- [ ] `dashboard/modules/console_view.js`
  - [ ] Mirror of the kernel VGA console output
  - [ ] Optional: input field to send keystrokes to kernel
- [ ] `dashboard/modules/fault_view.js`
  - [ ] Red alert panel for kernel panics
  - [ ] Displays panic reason, register dump, and faulting thread

### 17.3 — Dashboard Layout & Polish

- [ ] Top bar: system status, uptime, IRQ rate, context-switch counter
- [ ] Left panel: thread list + scheduler queue
- [ ] Center panel: switchable main view (boot timeline → scheduler → context switch animation)
- [ ] Right panel: registers + memory map + thread inspector
- [ ] Bottom panel: interrupt log + event stream
- [ ] Responsive layout for presenter/external display mode
- [ ] Smooth transitions between panel views
- [ ] Loading / connecting state when WebSocket is not yet live

---

## Phase 18 — Integration & Demo Flow

> **Goal**: Wire everything together into a seamless boot-to-demo experience.

- [ ] End-to-end test: build → QEMU boot → telemetry → bridge → dashboard → live visualization
- [ ] Implement the demonstration sequence from `visual.md`:
  1. [ ] Boot animation on dashboard
  2. [ ] Memory layout reveal
  3. [ ] Interrupt setup visualization
  4. [ ] Scheduler activation
  5. [ ] Live thread switching
  6. [ ] Register and TCB inspection
  7. [ ] Keyboard / shell interaction
  8. [ ] Controlled panic demo
  9. [ ] Performance summary display
- [ ] Write `tools/run.sh` to launch all three components (QEMU + bridge + dashboard server) with a single command
- [ ] Verify: Full demo runs reliably from cold start

---

## Phase 19 — Stretch Goals (Post-Baseline)

> **Goal**: Advanced features once the baseline is stable.

- [ ] Dynamic thread priorities with aging / starvation control (advanced)
- [ ] Software loader: load APP.BIN from disk, create thread, execute
- [ ] Performance benchmark command: measure context-switch time, IRQ latency, boot time
- [ ] Interactive event replay on the dashboard
- [ ] Historical charts (interrupts over time, scheduling distribution)
- [ ] Color-coded dependency graph of kernel subsystems on dashboard
- [ ] Side-by-side thread comparison over time

---

## User Review Required

> [!IMPORTANT]
> **Cross-compiler choice**: The plan assumes `i686-elf-gcc` as the cross-compiler targeting 16-bit real mode via `-m16` flag. If the project is strictly emu8086-only (no GCC), the C portions would need to be rewritten in pure assembly. Please confirm the emulator and toolchain preference.

> [!WARNING]
> **Scope**: The baseline (Phases 1–18) is substantial. Phases 12–13 (sync primitives, IPC) and Phase 16 (telemetry) add significant complexity. If time is constrained, these can be deferred to stretch without breaking the core demo.

## Open Questions

1. **Emulator target**: Is the primary target QEMU or emu8086? QEMU is preferred for serial-port telemetry redirection, but emu8086 may be required for classroom use. This affects how telemetry is emitted.
2. **Pure assembly vs. C**: The plan uses C for readability in kernel logic. If the requirement is 100% assembly + C++ only (no C), the kernel code will be significantly longer but doable. Please confirm.
3. **Dashboard hosting**: Should the dashboard be a static file opened locally, or served via a simple HTTP server? The plan assumes a local `python -m http.server` or similar.
4. **Team size**: Are there multiple developers, or is this a solo project? This affects how phases are parallelized.

---

## Verification Plan

### Automated Tests

- `make image` — confirms the full build pipeline produces a valid floppy image
- `make run` — boots QEMU and validates boot chain reaches kernel
- Each phase has explicit verification criteria listed above

### Manual Verification

- Visual confirmation that QEMU console shows expected output at each phase
- Dashboard receives and renders telemetry correctly (verified via browser)
- Full demo sequence runs end-to-end without crashes or visual glitches

---

## Success Criteria

The project is complete when:

1. ✅ System boots from raw floppy image without DOS
2. ✅ Two-stage boot chain loads kernel cleanly
3. ✅ At least 2 threads run under preemptive scheduling
4. ✅ Timer and keyboard interrupts are stable
5. ✅ Console and keyboard interaction work in kernel shell
6. ✅ INT 80h system calls function correctly
7. ✅ Dashboard displays live telemetry (boot, threads, interrupts, registers)
8. ✅ Full demo sequence is repeatable from cold start
9. ✅ Codebase is documented and modular enough for independent continuation
