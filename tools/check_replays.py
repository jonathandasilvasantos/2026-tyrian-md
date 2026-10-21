#!/usr/bin/env python3
"""Compare host/68000 replay cases, optionally recording verified goldens.

Manifest rows: episode hash, or episode left-sidekick right-sidekick hash.
Recording requires both implementations to agree; normal checks never write.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def state_hash(command, env):
    output = subprocess.check_output(command, text=True, env=env)
    match = re.search(r"(?:frames|ticks) \d+ hash ([0-9a-f]{8})\s*$", output)
    if not match:
        raise ValueError(f"missing final state hash from {command[0]}")
    return match[1]


def check(manifest, replay, record=False, ticks=600):
    lines = Path(manifest).read_text().splitlines()
    for index, line in enumerate(lines):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) not in (2, 4):
            raise ValueError(f"{manifest}:{index + 1}: expected episode [left right] hash")
        episode, expected = fields[0], fields[-1]
        loadout = fields[1:3] if len(fields) == 4 else []
        env = dict(os.environ, B68K_EPISODE=episode)
        env.pop("B68K_OPTIONS", None)
        if loadout:
            env["B68K_OPTIONS"] = ",".join(loadout)
        host = state_hash(["out/tyrian_host", str(ticks), replay, episode, *loadout], env)
        rom = state_hash(["out/bench68k", "out/rom.bin", "out/rom.syms", str(ticks), replay], env)
        if host != rom:
            raise ValueError(f"episode {episode} options {loadout}: host {host}, ROM {rom}")
        if not record and host != expected:
            raise ValueError(f"episode {episode} options {loadout}: expected {expected}, got {host}")
        if record:
            lines[index] = " ".join([*fields[:-1], host])
        print(f"   PASS  replay episode {episode} options {','.join(loadout) or 'none'} ({host})")
    if record:
        Path(manifest).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest")
    parser.add_argument("replay")
    parser.add_argument("--record", action="store_true")
    args = parser.parse_args()
    check(args.manifest, args.replay, args.record)
