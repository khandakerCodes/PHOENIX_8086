#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — 8086 instruction-set check

Disassembles the code sections of kernel.elf and fails if any
instruction is not available on an Intel 8086/8088:
unknown mnemonics, 32-bit registers or prefixes, FS/GS, two-byte
(0F xx) opcodes, PUSH imm, IMUL imm, and shifts by a count other
than 1 or CL.

The boot sectors are covered separately by NASM's "CPU 8086".

Usage: check8086.py <objdump> build/kernel.elf
"""

import re
import subprocess
import sys

PREFIXES = {"rep", "repz", "repnz", "repe", "repne", "lock",
            "cs", "ds", "es", "ss"}

JCC = """jo jno jb jnb jae jnae jc jnc je jz jne jnz jbe jna ja jnbe js jns
         jp jpe jnp jpo jl jnge jge jnl jle jng jg jnle jcxz""".split()

ALLOWED = set("""
    aaa aad aam aas adc add and call lcall cbtw cwtd clc cld cli cmc cmp cmps
    daa das dec div hlt idiv imul in inc int int3 into iret jmp ljmp lahf lds
    lea les lods loop loope loopne loopz loopnz mov movs mul neg nop not or
    out pop popf push pushf rcl rcr ret lret rol ror sahf sal sar sbb scas
    shl shr stc std sti stos sub test wait fwait xchg xlat xor
""".split()) | set(JCC)

SHIFTS = {"rcl", "rcr", "rol", "ror", "sal", "sar", "shl", "shr"}

BAD_OPERAND = re.compile(r"%e[a-d]x|%e[sd]i|%e[sbi]p|%[fg]s|%[cd]r\d|data32|addr32")
KERNEL_HDR_SIZE = 16  # include/layout.h

LINE = re.compile(r"^\s*([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t(.*)$")


def base_mnemonic(m):
    """Strip the AT&T operand-size suffix (b/w) if needed."""
    if m in ALLOWED:
        return m
    if m[-1] in "bw" and m[:-1] in ALLOWED:
        return m[:-1]
    return None


def check(addr, raw, text):
    tokens = text.split()
    while tokens and tokens[0] in PREFIXES:
        tokens.pop(0)
    if not tokens:
        return None
    mnem, operands = tokens[0], " ".join(tokens[1:])

    opcode_bytes = raw.split()
    while opcode_bytes and opcode_bytes[0] in ("26", "2e", "36", "3e", "f0", "f2", "f3"):
        opcode_bytes.pop(0)
    if opcode_bytes and opcode_bytes[0] == "0f":
        return "two-byte opcode (286+)"

    if BAD_OPERAND.search(text):
        return "32-bit operand, prefix or 386 register"

    base = base_mnemonic(mnem)
    if base is None:
        return f"mnemonic '{mnem}' is not 8086"

    if base == "push" and operands.startswith("$"):
        return "PUSH immediate (186+)"
    if base == "imul" and operands.startswith("$"):
        return "IMUL immediate (186+)"
    if base in SHIFTS:
        m = re.match(r"\$(0x[0-9a-f]+|\d+),", operands)
        if m and int(m.group(1), 0) != 1:
            return "shift/rotate by immediate count (186+)"
    return None


def main():
    objdump, elf = sys.argv[1], sys.argv[2]
    # The first 16 bytes of .text are the image header (data, not code)
    out = subprocess.run([objdump, "-d", "-mi8086",
                          f"--start-address={KERNEL_HDR_SIZE}", elf], check=True,
                         capture_output=True, text=True).stdout

    total, failures = 0, []
    for line in out.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        addr, raw, text = m.groups()
        text = text.split("#")[0].strip()
        if not text:
            continue  # continuation line of a long instruction
        total += 1
        problem = check(addr, raw, text)
        if problem:
            failures.append(f"  {addr}: {text:<40} {problem}")

    if failures:
        print(f"8086 check FAILED: {len(failures)} of {total} instructions")
        print("\n".join(failures[:40]))
        sys.exit(1)
    if total == 0:
        sys.exit("8086 check: no instructions found")
    print(f"  8086 check: {total} instructions, all 8086-compatible")


if __name__ == "__main__":
    main()
