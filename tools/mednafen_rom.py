#!/usr/bin/env python3
"""Prepare an emulator-only copy: Mednafen 1.32 ignores SEGA SSF headers.

Its SSF2 board is selected by the product code in src/md/cart/cart.cpp.
The hardware ROM keeps Tyrian's identity. Only this launch copy uses the
SSF2 database identifier; payload, console header and checksum stay intact.
"""
from pathlib import Path
import sys


def prepare(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve():
        raise ValueError("Mednafen copy must not overwrite the hardware ROM")
    data = bytearray(source.read_bytes())
    if len(data) < 0x200:
        raise ValueError("ROM is shorter than the Genesis header")
    if data[0x100:0x108] != b"SEGA SSF":
        return str(source)
    data[0x180:0x18E] = b"GM T-12056 -00"
    if not destination.exists() or destination.read_bytes() != data:
        destination.write_bytes(data)
    return str(destination)


if __name__ == "__main__":
    print(prepare(sys.argv[1], sys.argv[2]))
