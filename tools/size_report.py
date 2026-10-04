#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Size report and budget check

Prints how full the kernel's two 64 KB segments are, the largest
functions and variables, and the size of every program, then fails if
a segment has passed its budget (90% of its limit by default).

    size_report.py NM kernel.bin kernel.elf [program.BIN ...]
                   [--budget PERCENT] [--markdown FILE]

The limits are the ones the build already enforces: the code segment
holds at most FE00h bytes (tools/mkimage.py) and static data must end
by E000h, leaving room for the near heap and the kernel stack
(linker/kernel.ld). Failing at 90% gives warning before a hard limit.

With --markdown, the report is also written as a Markdown table (CI
appends it to the job summary).
"""

import argparse
import os
import struct
import subprocess
import sys

CODE_LIMIT = 0xFE00         # tools/mkimage.py MAX_IMAGE
DATA_LIMIT = 0xE000         # linker/kernel.ld: _kernel_end
DATA_RESERVED = 16          # offset 0 of the data segment is never used
SEGMENT = 0x10000
HEADER = struct.Struct("<4sHHHHHH")     # include/layout.h


def kernel_sizes(path):
    with open(path, "rb") as f:
        magic, _version, _image, text, data, bss, _checksum = HEADER.unpack(f.read(HEADER.size))
    if magic != b"PX86":
        sys.exit(f"{path}: not a Phoenix-8086 kernel image")
    return text, data, bss


def largest_symbols(nm, elf, count=6):
    """The biggest functions and the biggest variables, from nm --size-sort."""
    result = subprocess.run([nm, "--size-sort", "--reverse-sort", "-S", elf],
                            capture_output=True, text=True, check=True)
    code, data = [], []
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        size, kind, name = int(parts[1], 16), parts[2].lower(), parts[3]
        if kind == "t":
            code.append((name, size))
        elif kind in ("b", "d", "r"):
            data.append((name, size))
    return code[:count], data[:count]


def percent(used, limit):
    return 100.0 * used / limit


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    parser.add_argument("nm")
    parser.add_argument("kernel_bin")
    parser.add_argument("kernel_elf")
    parser.add_argument("programs", nargs="*")
    parser.add_argument("--budget", type=float, default=90.0,
                        help="fail when a segment is fuller than this percentage of its limit")
    parser.add_argument("--markdown", help="also write the report as Markdown to this file")
    args = parser.parse_args()

    text, data, bss = kernel_sizes(args.kernel_bin)
    static = DATA_RESERVED + data + bss
    heap_and_stack = SEGMENT - static
    code_symbols, data_symbols = largest_symbols(args.nm, args.kernel_elf)

    segments = [
        ("Code segment", text, CODE_LIMIT, "code"),
        ("Data segment (static)", static, DATA_LIMIT, f"{data} data + {bss} bss"),
    ]

    lines = ["Phoenix-8086 size report", ""]
    md = ["### Phoenix-8086 size report", "",
          "| Segment | Used | Limit | Full | Contents |", "| --- | ---: | ---: | ---: | --- |"]
    over = []
    for name, used, limit, contents in segments:
        full = percent(used, limit)
        flag = "  OVER BUDGET" if full > args.budget else ""
        lines.append(f"  {name:<22} {used:>6} of {limit:>6} bytes  {full:5.1f}%  ({contents}){flag}")
        md.append(f"| {name} | {used} | {limit} | {full:.1f}% | {contents} |")
        if flag:
            over.append(name)
    lines.append(f"  {'Heap + kernel stack':<22} {heap_and_stack:>6} bytes left in the data segment")
    md.append(f"| Heap + kernel stack | {heap_and_stack} | | | what is left of the data segment |")

    lines += ["", "  Largest functions:"]
    lines += [f"    {size:>5}  {name}" for name, size in code_symbols]
    lines += ["  Largest variables:"]
    lines += [f"    {size:>5}  {name}" for name, size in data_symbols]
    md += ["", "| Largest functions | Bytes | Largest variables | Bytes |", "| --- | ---: | --- | ---: |"]
    for i in range(max(len(code_symbols), len(data_symbols))):
        c = code_symbols[i] if i < len(code_symbols) else ("", "")
        d = data_symbols[i] if i < len(data_symbols) else ("", "")
        md.append(f"| `{c[0]}` | {c[1]} | `{d[0]}` | {d[1]} |")

    if args.programs:
        lines += ["", "  Programs:"]
        md += ["", "| Program | Bytes |", "| --- | ---: |"]
        for path in args.programs:
            size = os.path.getsize(path)
            lines.append(f"    {size:>5}  {os.path.basename(path)}")
            md.append(f"| {os.path.basename(path)} | {size} |")

    print("\n".join(lines))
    if args.markdown:
        with open(args.markdown, "a", encoding="utf-8") as f:
            f.write("\n".join(md) + "\n")

    if over:
        sys.exit(f"Size budget exceeded ({args.budget:.0f}% of the limit): {', '.join(over)}")


if __name__ == "__main__":
    main()
