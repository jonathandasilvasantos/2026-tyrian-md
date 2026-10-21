#!/usr/bin/env python3
"""Reject an 8 MB cartridge whose code/tables/audio reach the mapper window."""
import subprocess
import sys


def check(path):
    sections = subprocess.check_output(["m68k-elf-objdump", "-h", path], text=True)
    for line in sections.splitlines():
        fields = line.split()
        if len(fields) < 7 or not fields[0].isdigit():
            continue
        name, size, vma, lma = fields[1], *(int(v, 16) for v in fields[2:5])
        if name == ".far_assets":
            if vma != 0x380000 or lma + size > 0x800000:
                raise ValueError("far graphics exceed the 8 MB cartridge")
        elif name in (".text", ".data") and size and lma + size > 0x380000:
            raise ValueError(f"{name} reaches the switchable ROM window: {lma + size:#x}")
    print("   PASS  cartridge layout (fixed code/tables/audio below 3.5 MB)")


if __name__ == "__main__":
    check(sys.argv[1])
