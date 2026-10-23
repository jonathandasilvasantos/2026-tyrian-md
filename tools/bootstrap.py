#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fetch pinned external dependencies; never vendor them into the repository."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "tools/dependencies.lock.json").read_text())
DATA_SUFFIXES = {".dat", ".hdt", ".lvl", ".shp", ".pic", ".mus", ".snd", ".pcx", ".anm", ".doc"}


def run(*args):
    subprocess.run(args, check=True)


def git_dependency(root, spec):
    dest = root / spec["directory"]
    if dest.exists():
        revision = subprocess.check_output(["git", "-C", str(dest), "rev-parse", "HEAD"], text=True).strip()
        if revision != spec["revision"]:
            raise RuntimeError(f"{dest}: expected {spec['revision']}, found {revision}; leaving your checkout untouched")
        print(f"OK: {spec['directory']} at {revision}", flush=True)
        return
    run("git", "clone", "--filter=blob:none", "--no-checkout", spec["url"], str(dest))
    run("git", "-C", str(dest), "checkout", "--detach", spec["revision"])


def extract_data(archive, dest):
    """Flatten only game data/documents; never extract paths or DOS executables."""
    with zipfile.ZipFile(archive) as zipped:
        files = [entry for entry in zipped.infolist() if not entry.is_dir()
                 and Path(entry.filename).suffix.lower() in DATA_SUFFIXES]
        if not any(Path(entry.filename).name.lower() == "palette.dat" for entry in files):
            raise RuntimeError("Archive has no Tyrian palette.dat")
        dest.mkdir(parents=True, exist_ok=True)
        names = set()
        for entry in files:
            name = Path(entry.filename).name.lower()
            if name in names:
                raise RuntimeError(f"Duplicate data filename: {name}")
            names.add(name)
            (dest / name).write_bytes(zipped.read(entry))


def game_data(root):
    spec = LOCK["data"]
    dest = root / spec["directory"]
    required = ["palette.dat", "tyrian.hdt", "tyrian.shp", "music.mus"]
    required += [f"tyrian{i}.lvl" for i in range(1, 5)]
    if all((dest / name).is_file() for name in required):
        print(f"OK: local Tyrian data in {dest}", flush=True)
        return
    cache = root / "out/downloads"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "tyrian21.zip"
    if not archive.exists():
        print(f"Downloading freeware data from {spec['url']}", flush=True)
        with tempfile.NamedTemporaryFile(dir=cache, delete=False) as temporary:
            pending = Path(temporary.name)
            try:
                with urllib.request.urlopen(spec["url"], timeout=90) as response:
                    shutil.copyfileobj(response, temporary)
                temporary.close()
                if hashlib.sha256(pending.read_bytes()).hexdigest() != spec["sha256"]:
                    raise RuntimeError("Tyrian archive SHA-256 mismatch; nothing extracted")
                pending.replace(archive)
            finally:
                pending.unlink(missing_ok=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise RuntimeError(f"SHA-256 mismatch: remove {archive} and retry")
    extract_data(archive, dest)
    if not all((dest / name).is_file() for name in required):
        raise RuntimeError("Incomplete Tyrian data archive")
    print(f"OK: verified data extracted locally to {dest}; no asset license is changed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-only", action="store_true", help="download only the Tyrian game data")
    parser.add_argument("--root", type=Path, default=ROOT, help="dependency destination (default: project root)")
    args = parser.parse_args()
    args.root.mkdir(parents=True, exist_ok=True)
    if not args.data_only:
        for name in ("opentyrian", "sgdk"):
            git_dependency(args.root, LOCK[name])
    game_data(args.root)


if __name__ == "__main__":
    main()
