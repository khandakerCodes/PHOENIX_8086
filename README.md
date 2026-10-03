# Phoenix-8086

A small preemptive operating system kernel for the Intel 8086 that explains itself while it runs. It boots from a floppy image without DOS, runs threads under a timer-driven scheduler, and streams telemetry to a web dashboard.

**Status: pre-alpha.** The kernel works and is tested in QEMU and on an emulated 8086 (DOSBox-X). It has not yet been run on real hardware (see [Known limitations](#known-limitations)).

## What works

* Two-stage bootloader that loads a checksummed kernel image
* Kernel built as true 16-bit code, restricted to 8086 instructions (checked on every build)
* Preemptive scheduler: priorities, round-robin time slices, aging
* Threads with their own stacks, stack-overflow detection, sleep, kill
* Blocking semaphores, mutexes, and mailboxes
* System calls through `INT 80h` ([reference](docs/syscalls.md))
* Near heap and a far-memory allocator for the memory above the kernel
* FAT12 boot disk: the kernel lists and reads files from it
* Programs: separately built files loaded from the disk and run as threads, with an SDK ([guide](docs/programs.md))
* Panic screen with the live register state; CPU exception traps
* Interactive shell with demos, a benchmark, and an in-kernel self-test

## Quick start

Requirements: Linux on x86-64 (developed on Ubuntu 24.04, including WSL2), with `make`, `nasm`, `python3`, `curl`, `dpkg-deb`, and `qemu-system-i386`.

```sh
sudo apt install make nasm python3 curl qemu-system-x86

make toolchain   # fetch the ia16-elf-gcc cross compiler into .toolchain/ (about 200 MB, no root needed)
make test        # build, check the instruction set, run the unit tests, boot in QEMU and drive the shell
make run         # boot in a QEMU window

sudo apt install dosbox-x
make test-8086   # boot and drive the same image on an emulated 8086
```

`make toolchain` downloads prebuilt Ubuntu 24.04 packages. On other systems, install `gcc-ia16-elf` yourself (see [tkchia/build-ia16](https://github.com/tkchia/build-ia16)); the Makefile uses `ia16-elf-gcc` from `PATH` when `.toolchain/` is absent.

At the `phoenix>` prompt, type `help`. Good first commands:

| Command | What it shows |
| --- | --- |
| `create` (run it three times) | Three CPU-bound threads sharing the processor |
| `ps` | Thread table with states and CPU time |
| `ipc` | A producer and a consumer talking through a mailbox |
| `syscall` | A thread that uses only `INT 80h` |
| `ls`, `cat readme.txt` | The files on the FAT12 boot disk |
| `run primes.bin` | A program loaded from the disk |
| `memory` | The memory map and allocator state |
| `selftest` | The in-kernel unit tests |
| `cpu` | Which processor family the kernel detects |
| `overflow` | A runaway recursion being stopped |
| `panic` | The panic screen |

## Is it really 8086 code?

Three checks, all run in CI:

1. **Static:** `make check` disassembles the kernel and fails on any instruction that does not exist on an 8086. The boot sectors are assembled with NASM's `CPU 8086`.
2. **Dynamic:** `make test-8086` boots the image on DOSBox-X configured as an 8086 and runs the self-tests, the threading demos, mailbox IPC and system calls there.
3. **Control:** the kernel's `cpu` command identifies the processor using 8086-legal instructions. It answers "8086/8088" on the emulated 8086 and "80286 or later" under QEMU, which shows the two environments really differ.

## Dashboard

The kernel streams telemetry over its serial port: context switches with the resumed registers, thread states, counters, memory, system calls, console text, and faults. A bridge relays it to a web dashboard.

```sh
pip install -r bridge/requirements.txt
./phoenix.sh
```

This starts QEMU, the bridge, and a web server for the dashboard at <http://localhost:8080>. The console tab can type into the kernel's shell.

The dashboard always says where its data comes from:

| Badge | Meaning |
| --- | --- |
| LIVE | A running kernel, relayed by the bridge |
| REPLAY | A recorded session being played back |
| DEMO | Simulated data, only after you press "Run demo" |
| OFFLINE | No bridge; panels show "no data" or the last data received |

To record a session and replay it later without a kernel:

```sh
python3 tools/record_session.py build/phoenix8086.img session.jsonl
python3 bridge/serial_ws_bridge.py --replay session.jsonl --speed 2
make dashboard        # in another terminal, then open http://localhost:8080
```

The protocol is documented in [docs/telemetry.md](docs/telemetry.md). `make TELEMETRY=0` builds a kernel without it.

## Repository layout

| Path | Contents |
| --- | --- |
| `boot/` | Stage 1 boot sector and Stage 2 loader (NASM) |
| `kernel/` | Kernel sources (C and GNU assembler) |
| `include/` | Shared types and the memory layout |
| `linker/` | Kernel linker script |
| `sdk/` | Header, startup code, linker script and builder for programs, with examples |
| `disk/` | Files copied onto the boot floppy |
| `tools/` | Toolchain fetcher, image builders, instruction-set check, tests |
| `bridge/` | Telemetry decoder, capture files, and the serial-to-WebSocket bridge (Python) |
| `dashboard/` | Web dashboard |
| `docs/` | System-call and telemetry references, dashboard design, archived documents |

## Documentation

* [Project specification](projectdetails.md) — what the project is and what v1.0 means
* [Implementation plan](implementation_plan.md) — audit findings, phases, and current progress
* [System calls](docs/syscalls.md)
* [Writing programs](docs/programs.md)
* [Telemetry protocol](docs/telemetry.md)
* [Dashboard design](docs/observatory.md)

## Known limitations

* Not yet run on real hardware. `make test-8086` runs the kernel on DOSBox-X with its CPU set to 8086, which that project labels experimental; it is not a cycle-accurate 8088 and its BIOS is not an IBM PC BIOS.
* The dashboard's rendering has not been checked in a real browser yet. Its data model is tested against a recorded kernel session, and the rendering code was exercised against a stand-in page, but nobody has looked at it.
* Under heavy load (the `bench` command) the kernel's telemetry buffer fills and records are dropped. The drops are counted and shown, never hidden.
* Real mode has no memory protection: any thread can overwrite any memory.
* Disk reads go through the BIOS and pause the scheduler while they run; the file system is read-only.
* Programs share the kernel's data segment; there is no process isolation.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The [implementation plan](implementation_plan.md) lists open work.

## License

[MIT](LICENSE)
