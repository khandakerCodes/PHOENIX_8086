# Phoenix-8086 Documentation

| Document | What it covers |
| --- | --- |
| [Architecture](architecture.md) | How the system fits together: boot, memory, threads, interrupts, storage, telemetry |
| [Building and running](building.md) | Toolchain, build targets, tests, emulators, the dashboard and the demo site |
| [System calls](syscalls.md) | The `INT 80h` interface |
| [Writing programs](programs.md) | The SDK, the program file format, how programs are loaded |
| [Telemetry protocol](telemetry.md) | The serial protocol between the kernel and the dashboard |
| [Dashboard design](observatory.md) | Original design goals for the dashboard |
| [Translating](translating.md) | Adding a dashboard language or a keyboard layout |
| [Labs](labs/README.md) | Guided exercises for courses and self-study |

Project-level documents live in the repository root: the [specification](../projectdetails.md), the [implementation plan](../implementation_plan.md) with current status, and the [changelog](../CHANGELOG.md).

English is the reference language. Translations of these documents go in `docs/<language code>/` with the same file names; see [Translating](translating.md).
