#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Floppy image builder

Builds the bootable 1.44 MB floppy image as a valid FAT12 volume:

    LBA 0         Stage 1 boot sector (carries the BIOS Parameter Block)
    LBA 1-4       Stage 2 loader
    LBA 5...      Kernel image (inside the reserved sectors)
    then          two FATs, the root directory, and the files

The layout is taken from the BIOS Parameter Block in the Stage 1
binary, so boot/stage1.asm is the single source of truth. File
timestamps are fixed, which makes the image reproducible.

Usage: mkfat12.py OUT.img STAGE1 STAGE2 KERNEL [FILE ...]
"""

import os
import struct
import sys

SECTOR = 512
STAGE2_LBA = 1
KERNEL_LBA = 5

ATTR_ARCHIVE = 0x20
ATTR_VOLUME = 0x08
FAT_EOF = 0xFFF

# 1 January 2026, 00:00:00 in FAT date/time encoding
FAT_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1
FAT_TIME = 0


def parse_bpb(boot):
    (bytes_per_sector, sectors_per_cluster, reserved, fats, root_entries,
     total, media, sectors_per_fat) = struct.unpack_from("<HBHBHHBH", boot, 11)
    if bytes_per_sector != SECTOR or boot[510:512] != b"\x55\xAA":
        sys.exit("Stage 1 does not carry a valid boot sector")
    return {
        "sectors_per_cluster": sectors_per_cluster, "reserved": reserved, "fats": fats,
        "root_entries": root_entries, "total": total, "media": media,
        "sectors_per_fat": sectors_per_fat, "label": boot[43:54],
    }


def short_name(path):
    """Convert a host file name to an 11-byte 8.3 directory name."""
    base, ext = os.path.splitext(os.path.basename(path).upper())
    ext = ext[1:]
    allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
    if not 1 <= len(base) <= 8 or len(ext) > 3 or not set(base + ext) <= allowed:
        sys.exit(f"{path}: not a valid 8.3 file name")
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def set_fat_entry(fat, cluster, value):
    offset = cluster + cluster // 2
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = value >> 4
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | (value >> 8)


def main():
    out, stage1_path, stage2_path, kernel_path = sys.argv[1:5]
    file_paths = sys.argv[5:]

    with open(stage1_path, "rb") as f:
        stage1 = f.read()
    with open(stage2_path, "rb") as f:
        stage2 = f.read()
    with open(kernel_path, "rb") as f:
        kernel = f.read()

    bpb = parse_bpb(stage1)
    cluster_bytes = bpb["sectors_per_cluster"] * SECTOR
    fat_lba = bpb["reserved"]
    root_lba = fat_lba + bpb["fats"] * bpb["sectors_per_fat"]
    root_sectors = (bpb["root_entries"] * 32 + SECTOR - 1) // SECTOR
    data_lba = root_lba + root_sectors
    clusters = (bpb["total"] - data_lba) // bpb["sectors_per_cluster"]

    if STAGE2_LBA * SECTOR + len(stage2) > KERNEL_LBA * SECTOR:
        sys.exit("Stage 2 overlaps the kernel")
    if KERNEL_LBA * SECTOR + len(kernel) > bpb["reserved"] * SECTOR:
        sys.exit(f"kernel ({len(kernel)} bytes) does not fit in the reserved sectors")

    image = bytearray(bpb["total"] * SECTOR)
    image[0:SECTOR] = stage1
    image[STAGE2_LBA * SECTOR:STAGE2_LBA * SECTOR + len(stage2)] = stage2
    image[KERNEL_LBA * SECTOR:KERNEL_LBA * SECTOR + len(kernel)] = kernel

    fat = bytearray(bpb["sectors_per_fat"] * SECTOR)
    set_fat_entry(fat, 0, 0xF00 | bpb["media"])
    set_fat_entry(fat, 1, 0xFFF)

    root = bytearray(root_sectors * SECTOR)
    entries = [struct.pack("<11sB10xHHHI", bpb["label"], ATTR_VOLUME, FAT_TIME, FAT_DATE, 0, 0)]

    next_cluster = 2
    names = set()
    for path in file_paths:
        with open(path, "rb") as f:
            data = f.read()
        name = short_name(path)
        if name in names:
            sys.exit(f"{path}: duplicate file name on the image")
        names.add(name)

        count = (len(data) + cluster_bytes - 1) // cluster_bytes
        if next_cluster + count > clusters + 2:
            sys.exit(f"{path}: the image is full")
        first = next_cluster if count else 0
        for i in range(count):
            cluster = next_cluster + i
            set_fat_entry(fat, cluster, FAT_EOF if i == count - 1 else cluster + 1)
        offset = (data_lba + (next_cluster - 2) * bpb["sectors_per_cluster"]) * SECTOR
        image[offset:offset + len(data)] = data
        next_cluster += count

        entries.append(struct.pack("<11sB10xHHHI", name, ATTR_ARCHIVE,
                                   FAT_TIME, FAT_DATE, first, len(data)))

    if len(entries) > bpb["root_entries"]:
        sys.exit("too many files for the root directory")
    for i, entry in enumerate(entries):
        root[i * 32:(i + 1) * 32] = entry

    for copy in range(bpb["fats"]):
        start = (fat_lba + copy * bpb["sectors_per_fat"]) * SECTOR
        image[start:start + len(fat)] = fat
    image[root_lba * SECTOR:root_lba * SECTOR + len(root)] = root

    with open(out, "wb") as f:
        f.write(image)

    free = (clusters + 2 - next_cluster) * cluster_bytes
    print(f"  Floppy image: FAT12, {len(file_paths)} files, {free // 1024} KB free")


if __name__ == "__main__":
    main()
