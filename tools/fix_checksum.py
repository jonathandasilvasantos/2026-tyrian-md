#!/usr/bin/env python3
"""
fix_checksum.py — Write the correct Sega Genesis ROM checksum.

The Genesis checksum is the 16-bit sum of every big-endian word from
ROM offset 0x200 to the end of the file (padding included), stored at
offset 0x18E in the ROM header.

Usage:  python3 scripts/fix_checksum.py out/rom.bin
"""

import os
import struct
import sys
import tempfile

CHECKSUM_OFFSET = 0x18E
ROM_DATA_START  = 0x200


def fix(path: str) -> None:
    with open(path, "rb") as f:
        data = bytearray(f.read())

    payload = data[ROM_DATA_START:]
    if len(payload) % 2:
        payload += b"\x00"

    chk = 0
    for i in range(0, len(payload), 2):
        chk = (chk + struct.unpack_from(">H", payload, i)[0]) & 0xFFFF

    old = struct.unpack_from(">H", data, CHECKSUM_OFFSET)[0]
    struct.pack_into(">H", data, CHECKSUM_OFFSET, chk)

    # SCRIPT-01: write atomically via temp file to avoid ROM corruption on interrupt
    dir_ = os.path.dirname(os.path.abspath(path))
    fd, tmp_path = tempfile.mkstemp(dir=dir_)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp_path, path)
    except Exception:
        os.unlink(tmp_path)
        raise

    print(f"  Checksum: 0x{old:04X} → 0x{chk:04X}  ({path})")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <rom.bin>")
        sys.exit(1)
    fix(sys.argv[1])
