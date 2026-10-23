#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Refuse vendored dependencies, game assets, binaries or secrets in public Git."""
import argparse
import re
import subprocess

FORBIDDEN = ("open-tyrian", "sgdk", "tyrian-data", "src/gen_data", "res/art", "res/gen", "res/sfx", "res/music", "out")
BINARY_SUFFIXES = (".bin", ".exe", ".zip", ".wav", ".vgm", ".mus", ".lvl", ".shp", ".pcx", ".hdt", ".pic")
SECRETS = re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|github_pat_[A-Za-z0-9_]{30,}|gh[pousr]_[A-Za-z0-9]{30,}|AKIA[0-9A-Z]{16}")


def blocked(path):
    return (any(path == prefix or path.startswith(prefix + "/") for prefix in FORBIDDEN)
            or path.lower().endswith(BINARY_SUFFIXES)
            or (path.lower().endswith((".png", ".jpg", ".jpeg")) and not path.startswith("docs/screenshots/")))


def audit(history=False):
    errors = []
    if history:
        rows = subprocess.check_output(["git", "rev-list", "--objects", "--all"], text=True).splitlines()
        objects = [(row.split(" ", 1) + [""])[:2] for row in rows]
    else:
        rows = subprocess.check_output(["git", "ls-files", "-s"], text=True).splitlines()
        objects = []
        for row in rows:
            metadata, path = row.split("\t", 1)
            mode, object_id, stage = metadata.split()
            if mode == "160000":
                errors.append(f"Submodule/gitlink must be downloaded separately: {path}")
            objects.append((object_id, path))
    seen = set()
    for object_id, path in objects:
        if path and blocked(path):
            errors.append(f"Excluded material tracked in Git: {path}")
        seen.add(object_id)
    paths = dict(objects)
    packed = subprocess.check_output(["git", "cat-file", "--batch"],
                                     input="".join(object_id + "\n" for object_id in sorted(seen)).encode())
    position = 0
    while position < len(packed):
        end = packed.index(b"\n", position)
        object_id, kind, size = packed[position:end].decode().split()
        start = end + 1
        content = packed[start:start + int(size)]
        position = start + int(size) + 1
        if kind == "blob" and SECRETS.search(content):
            errors.append(f"Possible secret in object {object_id[:12]} ({paths.get(object_id, '')}); value withheld")
    if errors:
        raise SystemExit("Public audit FAILED:\n" + "\n".join(sorted(set(errors))))
    print(f"PASS: {'all reachable history' if history else 'Git index'} contains no forbidden assets/dependency trees, ROMs or detected secret patterns")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true", help="inspect every reachable Git object, not only the index")
    audit(parser.parse_args().history)
