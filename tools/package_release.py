#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package a complete ROM locally; never add its embedded game data to Git."""
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def validate_rom(data):
    if len(data) != 8 * 1024 * 1024:
        raise ValueError("Expected the unified 8 MiB cartridge")
    if not data[0x100:0x110].startswith(b"SEGA SSF"):
        raise ValueError("Expected an SSF2 mapper header")
    checksum = sum(int.from_bytes(data[i:i + 2], "big") for i in range(0x200, len(data), 2)) & 0xFFFF
    if checksum != int.from_bytes(data[0x18E:0x190], "big"):
        raise ValueError("ROM header checksum mismatch")


def main():
    source = ROOT / "out/rom.bin"
    data = source.read_bytes()
    validate_rom(data)
    dest = ROOT / "release"
    dest.mkdir(exist_ok=True)
    target = dest / "tyrian-md.bin"
    shutil.copyfile(source, target)
    digest = hashlib.sha256(data).hexdigest()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())
    info = {"source_commit": commit, "working_tree_dirty": dirty, "rom": target.name,
            "bytes": len(data), "sha256": digest, "mapper": "Sega SSF2",
            "python": platform.python_version(),
            "compiler": subprocess.check_output(["m68k-elf-gcc", "--version"], text=True).splitlines()[0],
            "distribution": "LOCAL ONLY: contains third-party Tyrian game data; do not upload",
            "dependencies": json.loads((ROOT / "tools/dependencies.lock.json").read_text())}
    (dest / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
    (dest / "SHA256SUMS").write_text(f"{digest}  {target.name}\n")
    print(f"Local release: {target.relative_to(ROOT)} ({len(data):,} bytes)")
    print(f"SHA-256: {digest}")
    print("The ROM and generated metadata are ignored by Git; do not upload asset-bearing binaries.")


if __name__ == "__main__":
    main()
