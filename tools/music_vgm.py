#!/usr/bin/env python3
"""music.mus -> one VGM 1.61 per song for SGDK's XGM2 (tools/tyrdata/music.py).

    python3 tools/music_vgm.py OUT_DIR [--songs 0,5,7] [--wav]

Builds out/lds_dump (OpenTyrian's LDS player, tools/lds_dump/lds_dump.c) if
needed, writes OUT_DIR/song_XX.vgm (XX = 1-based song number as in the
game's play_song(n-1) calls) and prints per-song stats.  --wav also renders
the original through OpenTyrian's OPL emulator to OUT_DIR/song_XX_opl.wav.
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from tyrdata import data_path, music  # noqa: E402
from tyrdata.binfile import offset_table  # noqa: E402


def build_dump():
    exe = os.path.join(ROOT, "out", "lds_dump")
    subprocess.run(["make", "-s", "-C", ROOT, "out/lds_dump"], check=True)
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir")
    ap.add_argument("--songs", help="comma-separated 0-based song indexes (default: all)")
    ap.add_argument("--wav", action="store_true", help="also render the OPL original to WAV")
    a = ap.parse_args()
    exe = build_dump()
    mus = data_path("music.mus")
    with open(mus, "rb") as f:
        count = len(offset_table(f.read()))
    songs = [int(s) for s in a.songs.split(",")] if a.songs else range(count)
    os.makedirs(a.out_dir, exist_ok=True)
    total = 0
    print(f"{'song':>4} {'length':>7} {'loop at':>8} {'size':>7}  mapping (OPL ch -> F=FM n, P=PSG n, N=noise, -=dropped)")
    with tempfile.TemporaryDirectory() as tmp:
        for s in songs:
            name = f"song_{s + 1:02d}"
            wav = os.path.join(a.out_dir, name + "_opl.wav") if a.wav else None
            log = music.run_dump(exe, mus, s, os.path.join(tmp, "s.log"), wav)
            vgm, alloc, stats = music.convert(log)
            with open(os.path.join(a.out_dir, name + ".vgm"), "wb") as f:
                f.write(vgm)
            total += len(vgm)
            sec = log.end * 2 / music.TICK_RATE2
            loop = f"{log.loop * 2 / music.TICK_RATE2:7.1f}s" if log.loop is not None else "    none"
            m = []
            for c in range(9):
                k = alloc.get(c)
                tag = "-" if k is None else {"fm": "F", "psg": "P", "noise": "N"}[k[0]] + (str(k[1] + 1) if k[0] != "noise" else "")
                m.append(f"{c}:{tag}" if stats[c]["notes"] else f"{c}:.")
            print(f"{s + 1:4d} {sec:6.1f}s {loop} {len(vgm):7d}  {' '.join(m)}")
    print(f"total {total} bytes in {len(songs)} VGM files")


if __name__ == "__main__":
    main()
