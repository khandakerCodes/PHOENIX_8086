#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Floppy image and program file checks

Reads the built floppy image back with an independent FAT12 reader and
compares every file with its source, checks the boot chain's place in
the reserved sectors, validates the program files' headers, and runs
fsck.fat on the image when it is installed.

Usage: test_image.py build/phoenix8086.img build/kernel.bin FILE...
"""

import os
import shutil
import struct
import subprocess
import sys

SECTOR = 512
KERNEL_LBA = 5

failures = []


def check(name, ok, detail=""):
    print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
    if not ok:
        failures.append(f"{name}: {detail}")


def fat_entry(fat, cluster):
    offset = cluster + cluster // 2
    value = fat[offset] | (fat[offset + 1] << 8)
    return value >> 4 if cluster & 1 else value & 0xFFF


def read_volume(image):
    """Return {name: bytes} for every file in the root directory."""
    (bytes_per_sector, spc, reserved, fats, root_entries, total, _media, spf) = \
        struct.unpack_from("<HBHBHHBH", image, 11)
    fat = image[reserved * SECTOR:(reserved + spf) * SECTOR]
    root_lba = reserved + fats * spf
    data_lba = root_lba + (root_entries * 32 + SECTOR - 1) // SECTOR

    files = {}
    for i in range(root_entries):
        entry = image[root_lba * SECTOR + i * 32:root_lba * SECTOR + (i + 1) * 32]
        if entry[0] == 0:
            break
        if entry[0] == 0xE5 or entry[11] & 0x18:
            continue
        name = entry[0:8].decode().rstrip()
        ext = entry[8:11].decode().rstrip()
        cluster, size = struct.unpack_from("<HI", entry, 26)
        data = bytearray()
        while 2 <= cluster < 0xFF7 and len(data) < size:
            start = (data_lba + (cluster - 2) * spc) * SECTOR
            data += image[start:start + spc * SECTOR]
            cluster = fat_entry(fat, cluster)
        files[f"{name}.{ext}" if ext else name] = bytes(data[:size])

    geometry = {"bytes_per_sector": bytes_per_sector, "reserved": reserved, "fats": fats,
                "sectors_per_fat": spf, "total": total,
                "fat_copies_equal": all(
                    image[(reserved + n * spf) * SECTOR:(reserved + (n + 1) * spf) * SECTOR] == fat
                    for n in range(fats))}
    return files, geometry


def check_program(name, data):
    if data[:4] != b"PXE2":
        check(f"{name}: program magic", False, repr(data[:4]))
        return
    text, init, _bss, entry, data_start, reserved = struct.unpack_from("<6H", data, 4)
    expected = 16 + text + init
    check(f"{name}: header matches the file",
          expected == len(data) and entry < text and data_start == 16 and reserved == 0,
          f"expected {expected} bytes, file has {len(data)}")


def main():
    image_path, kernel_path = sys.argv[1], sys.argv[2]
    sources = sys.argv[3:]
    with open(image_path, "rb") as f:
        image = f.read()
    with open(kernel_path, "rb") as f:
        kernel = f.read()

    files, geometry = read_volume(image)

    check("image is 1.44 MB with 512-byte sectors",
          len(image) == 2880 * SECTOR and geometry["bytes_per_sector"] == SECTOR and geometry["total"] == 2880)
    check("boot signature present", image[510:512] == b"\x55\xAA")
    check("kernel sits in the reserved sectors",
          image[KERNEL_LBA * SECTOR:KERNEL_LBA * SECTOR + len(kernel)] == kernel and
          KERNEL_LBA * SECTOR + len(kernel) <= geometry["reserved"] * SECTOR)
    check("both FAT copies are identical", geometry["fat_copies_equal"])

    for source in sources:
        name = os.path.basename(source).upper()
        with open(source, "rb") as f:
            original = f.read()
        check(f"{name}: readable from the volume and identical to the source",
              files.get(name) == original,
              "missing" if name not in files else "contents differ")
        if name.endswith(".BIN"):
            check_program(name, original)
    check("no unexpected files", set(files) == {os.path.basename(s).upper() for s in sources},
          str(sorted(files)))

    fsck = shutil.which("fsck.fat") or (os.path.exists("/usr/sbin/fsck.fat") and "/usr/sbin/fsck.fat")
    if fsck:
        result = subprocess.run([fsck, "-n", image_path], capture_output=True, text=True)
        check("fsck.fat finds no errors", result.returncode == 0, result.stdout[-300:])
    else:
        print("  skip  fsck.fat not installed")

    if failures:
        print(f"Image test FAILED ({len(failures)}):")
        for failure in failures:
            print(f"  - {failure[:400]}")
        sys.exit(1)
    print("  Image test passed")


if __name__ == "__main__":
    main()
