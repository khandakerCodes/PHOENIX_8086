# Building and Running Phoenix-8086

## What has been verified

| Host | Status |
| --- | --- |
| Ubuntu 24.04 on x86-64 | Verified; this is what CI uses |
| Windows with WSL2 (Ubuntu 24.04) | Verified; the project is developed this way |
| Other Linux distributions | Should work if you install `gcc-ia16-elf` yourself; not verified |
| macOS | Not verified. You need an `ia16-elf-gcc` toolchain (build it from [tkchia/build-ia16](https://github.com/tkchia/build-ia16)), NASM, GNU Make, Python 3 and QEMU |

If you get it working somewhere not listed as verified, please open a pull request that updates this table.

## Tools

```sh
sudo apt install make nasm python3 curl qemu-system-x86
make toolchain        # ia16-elf-gcc into .toolchain/, about 200 MB, no root needed
```

`make toolchain` downloads prebuilt Ubuntu 24.04 packages from the toolchain maintainer's archive and verifies their checksums. If `.toolchain/` does not exist, the Makefile uses `ia16-elf-gcc` from your `PATH`.

Optional:

| Tool | Needed for | Install |
| --- | --- | --- |
| DOSBox-X | `make test-8086` | `sudo apt install dosbox-x` |
| mtools | The "outside tool" part of `make test` | `sudo apt install mtools` |
| Node.js 20 or later | Dashboard tests in `make test` | your package manager |
| `websockets` for Python | The telemetry bridge and its end-to-end tests | `pip install -r bridge/requirements.txt` |
| npm | The demo site | comes with Node.js |

## Make targets

| Target | What it does |
| --- | --- |
| `make` / `make all` | Build the boot sectors, kernel, example programs and floppy image |
| `make check` | Fail if the kernel or a program contains a non-8086 instruction |
| `make test` | `check`, host unit tests, image check, boot smoke test, integration test in QEMU |
| `make test-8086` | Boot and drive the kernel on DOSBox-X as an 8086 |
| `make soak SOAK_SECONDS=600` | Churn threads, IPC and programs for the given time |
| `make run` | Boot in a QEMU window |
| `make debug` | Boot in QEMU waiting for GDB on port 1234 |
| `make dashboard` | Serve the dashboard at http://localhost:8080 |
| `make clean` | Remove `build/` |

Options: `make TELEMETRY=0` builds a kernel without telemetry (run `make clean` when switching).

Outputs are in `build/`: `phoenix8086.img` (the floppy image), `kernel.elf` (with symbols), `kernel.bin`, and `programs/`.

## Running with the dashboard

```sh
pip install -r bridge/requirements.txt
./phoenix.sh
```

This builds, then starts the dashboard server (port 8080), the telemetry bridge (WebSocket port 9090), and QEMU with its serial port on TCP port 9876. If a port is taken the script says which and stops.

To do it by hand:

```sh
qemu-system-i386 -drive file=build/phoenix8086.img,format=raw,if=floppy -boot a -m 1M \
    -serial tcp:127.0.0.1:9876,server,nowait
python3 bridge/serial_ws_bridge.py --capture session.jsonl
make dashboard
```

Replay a capture without a kernel: `python3 bridge/serial_ws_bridge.py --replay session.jsonl --speed 2`.

## The demo site

```sh
make all
./tools/build_site.sh                       # assembles site/
node tools/test_browser_demo.mjs site       # needs: npm install v86
node tools/test_dashboard_browser.mjs       # needs: npm install playwright-core, and
                                            #   npx playwright-core install chromium-headless-shell
python3 -m http.server 8080 --directory site
```

The site boots the floppy image in v86, a PC emulator that runs in the browser, and shows it on the dashboard. The `Demo site` GitHub workflow builds and publishes it with GitHub Pages; enable Pages for the repository first (Settings → Pages → Source: GitHub Actions).

## Releases

Pushing a tag such as `v0.6.0` runs the `Release` workflow: it builds, runs `make test` and `make test-8086`, and publishes the floppy image, its checksum and a recorded telemetry session.

## Debugging

* `make debug`, then in another terminal: `gdb -ex 'target remote :1234' -ex 'set architecture i8086'`. Addresses are segment × 16 + offset: kernel code is at `0x10000` plus the offset shown in `build/kernel.elf`.
* The shell has `ps`, `memory`, `stats`, `interrupts`, `scheduler` and `registers`.
* `selftest` runs the in-kernel unit tests.
* A panic prints the registers at the point of failure; look the IP up in `ia16-elf-objdump -d build/kernel.elf`.

## Known compiler pitfall

`ia16-elf-gcc` 6.3 at `-Os` miscompiles calls such as `f((uint8_t)(x >> 8))` when `x` is 32 bits wide. Split the value into bytes in memory first; see `put32` in `kernel/telemetry.c`.
