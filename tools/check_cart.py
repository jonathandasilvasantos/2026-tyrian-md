#!/usr/bin/env python3
"""Reject an 8 MB cartridge whose code/tables/audio reach the mapper window."""
from pathlib import Path
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
    symbols = {}
    for line in subprocess.check_output(["m68k-elf-nm", "-n", path], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    raw = Path(path).with_suffix(".bin").read_bytes()
    player_palettes = []
    for ep in range(1, 5):
        address = symbols[f"ep{ep}_spr_palettes"]
        player_palettes.append(raw[address:address + 32])
    if any(len(pal) != 32 or pal != player_palettes[0] for pal in player_palettes):
        raise ValueError("player/bullet PAL2 must be identical across all episodes")
    print("   PASS  shared player/bullet palette across four episodes")
    print("   PASS  cartridge layout (fixed code/tables/audio below 3.5 MB)")


if __name__ == "__main__":
    check(sys.argv[1])
