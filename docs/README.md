# Phoenix-8086 Documentation

| Document | What it covers |
| --- | --- |
| **[User manual](manual.md)** | Using the kernel: the screen, every shell command, programs, the dashboard, troubleshooting |
| [Architecture](architecture.md) | How the system fits together: boot, memory, threads, scheduling, interrupts, storage, console, telemetry |
| [Building and running](building.md) | Toolchain, build targets and options, tests, screenshots, emulators, the dashboard and the demo site |
| [System calls](syscalls.md) | The `INT 80h` interface and the checks on program arguments |
| [Writing programs](programs.md) | The SDK, the program file format, how programs are loaded |
| [Telemetry protocol](telemetry.md) | The serial protocol between the kernel and the dashboard |
| [Dashboard design](observatory.md) | The original design goals for the dashboard (historical; the dashboard as built is described in the manual) |
| [Translating](translating.md) | Adding a dashboard language or a keyboard layout |
| [Labs](labs/README.md) | Guided exercises for courses and self-study |

Project-level documents live in the repository root: the [specification](../projectdetails.md), the [implementation plan](../implementation_plan.md) with status up to 1.0, the [upscaling plan](../UPSCALING.md) for everything after it, and the [changelog](../CHANGELOG.md).

Pictures are in `images/`; the console screenshots are made with `tools/screenshot.py` and the dashboard ones by the browser test (see [Building and running](building.md#screenshots)).

English is the reference language. Translations of these documents go in `docs/<language code>/` with the same file names; see [Translating](translating.md).
