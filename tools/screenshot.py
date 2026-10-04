#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Console screenshots

Boots the image in headless QEMU, types each command, waits, and saves
what the screen shows as a PNG (QEMU's monitor `screendump`).

    screenshot.py IMAGE OUT.png [--wait SECONDS] [--scale N] COMMAND...

A COMMAND of `@N` waits N seconds instead of typing. Used to check the
console's look while working on it and to make the README pictures.
"""

import argparse
import os
import sys
import tempfile
import time

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.integration_test import Machine  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    parser.add_argument("image")
    parser.add_argument("output")
    parser.add_argument("commands", nargs="*")
    parser.add_argument("--wait", type=float, default=1.5, help="seconds to wait after each command")
    parser.add_argument("--scale", type=int, default=1, help="enlarge the picture by this factor")
    args = parser.parse_intermixed_args()

    with tempfile.TemporaryDirectory() as tmp:
        machine = Machine(args.image, tmp)
        try:
            if machine.wait_for(r"phoenix", timeout=30) is None:
                sys.exit("the kernel did not reach the shell")
            time.sleep(1.0)
            for command in args.commands:
                if command.startswith("@"):
                    time.sleep(float(command[1:]))
                    continue
                machine.type(command)
                time.sleep(args.wait)
            dump = os.path.join(tmp, "screen.ppm")
            machine.qemu.stdin.write(f"screendump {dump}\n".encode())
            machine.qemu.stdin.flush()
            for _ in range(50):
                if os.path.exists(dump) and os.path.getsize(dump) > 0:
                    break
                time.sleep(0.1)
            time.sleep(0.3)
            picture = Image.open(dump)
            if args.scale > 1:
                picture = picture.resize((picture.width * args.scale, picture.height * args.scale),
                                         Image.NEAREST)
            picture.save(args.output)
        finally:
            machine.stop()
    print(f"{args.output}: {picture.width}x{picture.height}")


if __name__ == "__main__":
    main()
