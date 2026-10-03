#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Kernel image finalizer

Validates the kernel image header (see include/layout.h) and patches
the checksum field that the Stage 2 loader verifies.

Usage: mkimage.py build/kernel.bin
"""

import struct
import sys

MAGIC = b"PX86"
HDR_IMAGE_SIZE = 6
HDR_CHECKSUM = 14
MAX_IMAGE = 0xFE00


def main():
    path = sys.argv[1]
    with open(path, "rb") as f:
        image = bytearray(f.read())

    if image[:4] != MAGIC:
        sys.exit(f"{path}: bad magic, header is not at offset 0")

    (declared,) = struct.unpack_from("<H", image, HDR_IMAGE_SIZE)
    if declared != len(image):
        sys.exit(f"{path}: header says {declared} bytes, file is {len(image)}")
    if len(image) > MAX_IMAGE:
        sys.exit(f"{path}: {len(image)} bytes exceeds the {MAX_IMAGE}-byte limit")

    struct.pack_into("<H", image, HDR_CHECKSUM, 0)
    checksum = sum(image) & 0xFFFF
    struct.pack_into("<H", image, HDR_CHECKSUM, checksum)

    with open(path, "wb") as f:
        f.write(image)

    print(f"  Kernel image: {len(image)} bytes, checksum 0x{checksum:04X}")


if __name__ == "__main__":
    main()
