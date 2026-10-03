#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 SDK — Program file builder

Turns a program linked with sdk/program.ld into the file format the
kernel loads:

    offset  size  field
    0       4     magic "PXE2"
    4       2     text size in bytes
    6       2     data size in bytes (initialised data and constants)
    8       2     bss size in bytes (zeroed by the loader)
    10      2     entry point, an offset into the text
    12      2     offset of the data within the data segment (16)
    14      2     reserved, zero
    16      ...   text, then data

The text is loaded at offset 0 of a code segment and the data at its
linked offset in a separate data segment, so no addresses need
adjusting at load time.

Usage: mkprog.py program.elf PROGRAM.BIN
"""

import struct
import sys

MAGIC = b"PXE2"
DATA_START = 16

SHT_NOBITS = 8


class Elf:
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
            sys.exit("not a 32-bit little-endian ELF file")
        self.data = data
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        self.entry, = struct.unpack_from("<I", data, 0x18)

        self.sections = {}
        raw = []
        for i in range(shnum):
            name, kind, _flags, addr, offset, size = struct.unpack_from("<6I", data, shoff + i * shentsize)
            raw.append({"name_offset": name, "type": kind, "addr": addr, "offset": offset, "size": size})
        names = raw[shstrndx]
        for section in raw:
            start = names["offset"] + section["name_offset"]
            self.sections[data[start:data.index(b"\0", start)].decode()] = section

    def contents(self, name):
        section = self.sections.get(name)
        if section is None or section["type"] == SHT_NOBITS:
            return b""
        return self.data[section["offset"]:section["offset"] + section["size"]]


def main():
    source, out = sys.argv[1], sys.argv[2]
    with open(source, "rb") as f:
        elf = Elf(f.read())

    if ".text" not in elf.sections:
        sys.exit(f"{source}: no .text section")

    text = elf.contents(".text")
    data = elf.contents(".data")
    bss_size = elf.sections[".bss"]["size"] if ".bss" in elf.sections else 0

    if elf.sections[".text"]["addr"] != 0:
        sys.exit(f"{source}: the text must be linked at offset 0 (use sdk/program.ld)")
    if ".data" in elf.sections and elf.sections[".data"]["addr"] != DATA_START:
        sys.exit(f"{source}: the data must be linked at offset {DATA_START} (use sdk/program.ld)")
    if ".bss" in elf.sections and bss_size and \
            elf.sections[".bss"]["addr"] != DATA_START + len(data):
        sys.exit(f"{source}: the bss must follow the data directly")
    if len(text) > 0xFFF0 or DATA_START + len(data) + bss_size > 0xFFF0:
        sys.exit(f"{source}: the program is too large for its segments")
    if elf.entry >= len(text):
        sys.exit(f"{source}: entry point is outside the text")

    header = MAGIC + struct.pack("<6H", len(text), len(data), bss_size, elf.entry, DATA_START, 0)
    with open(out, "wb") as f:
        f.write(header + text + data)

    print(f"  {out}: text {len(text)}, data {len(data)}, bss {bss_size}")


if __name__ == "__main__":
    main()
