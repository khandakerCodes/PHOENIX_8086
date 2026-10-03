# Phoenix-8086

A small preemptive operating system kernel for the Intel 8086 that explains itself while it runs. It boots from a floppy image without DOS, runs threads under a timer-driven scheduler, and streams telemetry to a web dashboard.

**Status: pre-alpha.** The kernel works and is tested in QEMU. It has not yet been run on an 8086-only emulator or on real hardware, and the dashboard is not yet trustworthy (see [Known limitations](#known-limitations)).

## What works

* Two-stage bootloader that loads a checksummed kernel image
* Kernel built as true 16-bit code, restricted to 8086 instructions (checked on every build)
* Preemptive scheduler: priorities, round-robin time slices, aging
* Threads with their own stacks, stack-overflow detection, sleep, kill
* Blocking semaphores, mutexes, and mailboxes
* System calls through `INT 80h` ([reference](docs/syscalls.md))
* Near heap and a far-memory allocator for the memory above the kernel
* Panic screen with the live register state; CPU exception traps
* Interactive shell with demos, a benchmark, and an in-kernel self-test

## Quick start

Requirements: Linux on x86-64 (developed on Ubuntu 24.04, including WSL2), with `make`, `nasm`, `python3`, `curl`, `dpkg-deb`, and `qemu-system-i386`.

```sh
sudo apt install make nasm python3 curl qemu-system-x86

make toolchain   # fetch the ia16-elf-gcc cross compiler into .toolchain/ (about 200 MB, no root needed)
make test        # build, check the instruction set, boot in QEMU and run the shell tests
make run         # boot in a QEMU window
```

`make toolchain` downloads prebuilt Ubuntu 24.04 packages. On other systems, install `gcc-ia16-elf` yourself (see [tkchia/build-ia16](https://github.com/tkchia/build-ia16)); the Makefile uses `ia16-elf-gcc` from `PATH` when `.toolchain/` is absent.

At the `phoenix>` prompt, type `help`. Good first commands:

| Command | What it shows |
| --- | --- |
| `create` (run it three times) | Three CPU-bound threads sharing the processor |
| `ps` | Thread table with states and CPU time |
| `ipc` | A producer and a consumer talking through a mailbox |
| `syscall` | A thread that uses only `INT 80h` |
| `memory` | The memory map and allocator state |
| `selftest` | The in-kernel unit tests |
| `overflow` | A runaway recursion being stopped |
| `panic` | The panic screen |

## Dashboard

```sh
pip install -r bridge/requirements.txt
./phoenix.sh
```

This starts QEMU, the serial-to-WebSocket bridge, and a web server for the dashboard at <http://localhost:8080>.

## Repository layout

| Path | Contents |
| --- | --- |
| `boot/` | Stage 1 boot sector and Stage 2 loader (NASM) |
| `kernel/` | Kernel sources (C and GNU assembler) |
| `include/` | Shared types and the memory layout |
| `linker/` | Kernel linker script |
| `tools/` | Toolchain fetcher, image finalizer, instruction-set check, tests |
| `bridge/` | Serial-to-WebSocket telemetry bridge (Python) |
| `dashboard/` | Web dashboard |
| `docs/` | System-call reference, dashboard design, archived documents |

## Documentation

* [Project specification](projectdetails.md) — what the project is and what v1.0 means
* [Implementation plan](implementation_plan.md) — audit findings, phases, and current progress
* [System calls](docs/syscalls.md)
* [Dashboard design](docs/observatory.md)

## Known limitations

* Verified only in QEMU, which emulates a 386 or later. The instruction-set check covers the code, but nothing has yet confirmed a boot on an 8086-only machine.
* The dashboard falls back to simulated data when it has no connection, and parts of the scheduler view are not driven by real data.
* Telemetry frames and console text share one serial stream without escaping.
* Real mode has no memory protection: any thread can overwrite any memory.
* No file system or program loader yet.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The [implementation plan](implementation_plan.md) lists open work.

## License

[MIT](LICENSE)
