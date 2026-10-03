#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 SDK — Program file builder

Turns a program linked with sdk/program.ld (and "ld -q", which keeps
the relocations) into the file format the kernel loads:

    offset  size  field
    0       4     magic "PXE1"
    4       2     text size in bytes
    6       2     data size in bytes (initialised data and constants)
    8       2     bss size in bytes (zeroed by the loader)
    10      2     entry point, an offset into the text
    12      2     number of text relocations
    14      2     number of data relocations
    16      ...   text, then data, then the relocation tables

A program's data is placed wherever the kernel finds room, so every
16-bit reference to a data address must have that address added when
the program is loaded. A relocation is the 16-bit offset of such a
reference, within the text or within the data. References to code are
not relocated: code always starts at offset 0 of its own segment.

Usage: mkprog.py program.elf PROGRAM.BIN
"""

import struct
import sys

MAGIC = b"PXE1"

SHT_SYMTAB, SHT_REL, SHT_NOBITS = 2, 9, 8
SHF_ALLOC, SHF_EXECINSTR = 0x2, 0x4
SHN_UNDEF, SHN_ABS = 0, 0xFFF1
R_386_16, R_386_PC16 = 20, 21


class Elf:
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
            sys.exit("not a 32-bit little-endian ELF file")
        self.data = data
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        self.entry, = struct.unpack_from("<I", data, 0x18)

        self.sections = []
        for i in range(shnum):
            name, kind, flags, addr, offset, size, link, info, _, entsize = \
                struct.unpack_from("<10I", data, shoff + i * shentsize)
            self.sections.append({"name_offset": name, "type": kind, "flags": flags,
                                  "addr": addr, "offset": offset, "size": size,
                                  "link": link, "info": info, "entsize": entsize})
        names = self.sections[shstrndx]
        for section in self.sections:
            start = names["offset"] + section["name_offset"]
            section["name"] = data[start:data.index(b"\0", start)].decode()

    def section(self, name):
        for index, section in enumerate(self.sections):
            if section["name"] == name:
                return index, section
        return None, None

    def contents(self, section):
        if section is None or section["type"] == SHT_NOBITS:
            return b""
        return self.data[section["offset"]:section["offset"] + section["size"]]

    def symbols(self):
        table = next(s for s in self.sections if s["type"] == SHT_SYMTAB)
        result = []
        for offset in range(table["offset"], table["offset"] + table["size"], 16):
            _, value, _, _, _, shndx = struct.unpack_from("<IIIBBH", self.data, offset)
            result.append((value, shndx))
        return result

    def relocations(self, target_index):
        """Yield (offset, type, symbol index) for relocations applying to a section."""
        for section in self.sections:
            if section["type"] == SHT_REL and section["info"] == target_index:
                for offset in range(section["offset"], section["offset"] + section["size"], 8):
                    r_offset, r_info = struct.unpack_from("<II", self.data, offset)
                    yield r_offset, r_info & 0xFF, r_info >> 8


def data_relocations(elf, target_index, symbols):
    """Offsets in a section that hold the 16-bit address of something in the data segment."""
    offsets = []
    for offset, kind, symbol in elf.relocations(target_index):
        _, shndx = symbols[symbol]
        if kind == R_386_PC16:
            continue                    # relative jump or call: position-independent
        if kind != R_386_16:
            sys.exit(f"unsupported relocation type {kind} at offset {offset:#x}")
        if shndx == SHN_ABS:
            continue                    # a constant, not an address
        if shndx == SHN_UNDEF:
            sys.exit(f"undefined symbol referenced at offset {offset:#x}")
        if elf.sections[shndx]["flags"] & SHF_EXECINSTR:
            continue                    # address of code: the code segment starts at 0
        offsets.append(offset)
    return sorted(offsets)


def main():
    source, out = sys.argv[1], sys.argv[2]
    with open(source, "rb") as f:
        elf = Elf(f.read())

    text_index, text = elf.section(".text")
    data_index, data = elf.section(".data")
    _, bss = elf.section(".bss")
    if text is None:
        sys.exit(f"{source}: no .text section")

    text_bytes = elf.contents(text)
    data_bytes = elf.contents(data)
    bss_size = bss["size"] if bss else 0

    symbols = elf.symbols()
    text_relocs = data_relocations(elf, text_index, symbols)
    data_relocs = data_relocations(elf, data_index, symbols) if data else []

    for size, what in ((len(text_bytes), "text"), (len(data_bytes) + bss_size, "data + bss")):
        if size > 0xFFF0:
            sys.exit(f"{source}: {what} is too large ({size} bytes)")
    if elf.entry >= len(text_bytes):
        sys.exit(f"{source}: entry point is outside the text")

    header = MAGIC + struct.pack("<6H", len(text_bytes), len(data_bytes), bss_size,
                                 elf.entry, len(text_relocs), len(data_relocs))
    relocs = struct.pack(f"<{len(text_relocs) + len(data_relocs)}H", *text_relocs, *data_relocs)

    with open(out, "wb") as f:
        f.write(header + text_bytes + data_bytes + relocs)

    print(f"  {out}: text {len(text_bytes)}, data {len(data_bytes)}, bss {bss_size}, "
          f"relocations {len(text_relocs)}+{len(data_relocs)}")


if __name__ == "__main__":
    main()
