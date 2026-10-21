#!/usr/bin/env python3
"""Check selected scripted modes on host and both 68000 video regions.

Rows: episode mode level hash. Mode 1 = ENGAGE, 2 = ENGAGE + GALAGA.
Recording writes only after all three independently executed results agree.
"""
import argparse
import os
from pathlib import Path

from check_replays import state_hash


def check(manifest, replay, record=False):
    lines = Path(manifest).read_text().splitlines()
    for i, line in enumerate(lines):
        if not line.strip() or line.startswith('#'):
            continue
        episode, mode, level, expected = line.split()
        env = dict(os.environ, B68K_EPISODE=episode, B68K_MODE=mode, B68K_LEVEL=level)
        for key in ('B68K_OPTIONS', 'B68K_BOMBS', 'B68K_SPECIAL', 'B68K_CHECKPOINT', 'B68K_STORY', 'B68K_CUBES'):
            env.pop(key, None)
        host = state_hash(['out/tyrian_host', '600', replay, episode], env)
        for pal in (0, 1):
            rom = state_hash(['out/bench68k', 'out/rom.bin', 'out/rom.syms', '600', replay],
                             dict(env, B68K_PAL=str(pal)))
            if rom != host:
                raise ValueError(f'mode {mode} episode {episode} level {level}: host {host} ROM {rom} PAL={pal}')
        if not record and host != expected:
            raise ValueError(f'mode {mode} episode {episode} level {level}: expected {expected}, got {host}')
        if record:
            lines[i] = f'{episode} {mode} {level} {host}'
        print(f'   PASS  mode {mode} episode {episode} level {level} ({host}, NTSC/PAL)')
    if record:
        Path(manifest).write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest')
    parser.add_argument('replay')
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    check(args.manifest, args.replay, args.record)
