"""music.mus -> VGM converter (tools/tyrdata/music.py, tools/lds_dump)."""
import math
import os
import struct
import subprocess
import sys

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from tyrdata import data_path, music  # noqa: E402

HAS_DATA = os.path.exists(data_path("music.mus"))


def cents(a, b):
    return 1200 * math.log2(a / b)


@pytest.mark.parametrize("midi", range(36, 97))         # C2..C7
def test_frequency_roundtrip(midi):
    hz = 440 * 2 ** ((midi - 69) / 12)
    # OPL2 side: best fnum/block for the note (what LDS's table encodes)
    block = next(b for b in range(8) if round(hz * (1 << (20 - b)) / music.OPL_RATE) < 1024)
    fnum = round(hz * (1 << (20 - block)) / music.OPL_RATE)
    opl = music.opl_hz(fnum, block)
    assert abs(cents(opl, hz)) < 5
    f, b = music.opn_fnum(opl)
    assert f < 2048 and 0 <= b <= 7
    assert abs(cents(music.opn_hz(f, b), opl)) < 3
    p = music.psg_period(opl)
    psg = music.PSG_CLOCK / (32 * p)
    octaves = round(math.log2(psg / opl))                # sub-109 Hz notes go up
    assert octaves >= 0 and abs(cents(psg, opl * 2 ** octaves)) < 15


@pytest.fixture(scope="module")
def song1():
    if not HAS_DATA:
        pytest.skip("no tyrian-data")
    subprocess.run(["make", "-s", "-C", ROOT, "out/lds_dump"], check=True)
    log = music.run_dump(music.default_dump_exe(), data_path("music.mus"), 0,
                         os.path.join(ROOT, "out", "test_song1.log"))
    return log, music.convert(log)[0]


def test_vgm_header(song1):
    log, vgm = song1
    assert vgm[:4] == b"Vgm "
    eof, ver, sn = struct.unpack_from("<III", vgm, 4)
    assert eof == len(vgm) - 4 and ver == 0x161 and sn == 3579545
    assert struct.unpack_from("<I", vgm, 0x2C)[0] == 7670453
    assert struct.unpack_from("<I", vgm, 0x34)[0] + 0x34 == 0x100
    total = struct.unpack_from("<I", vgm, 0x18)[0]
    assert total == music.tick_sample(log.end)
    loop_ofs, loop_n = struct.unpack_from("<II", vgm, 0x1C)
    assert log.loop is not None and 0x100 <= loop_ofs + 0x1C < len(vgm)
    assert loop_n == total - music.tick_sample(log.loop)


def test_song1_commands(song1):
    _, vgm = song1
    seen, waited = set(), 0
    for cmd, args in music.vgm_commands(vgm):
        seen.add(cmd)
        if cmd == 0x61:
            waited += args[0] | args[1] << 8
        elif cmd == 0x62:
            waited += 735
        elif 0x70 <= cmd <= 0x7F:
            waited += cmd - 0x6F
        elif cmd == 0x52 and args[0] == 0x28:
            assert args[1] & 7 != 6, "FM channel 6 key-on"      # ch6 = port1 ch2 code 6
        elif cmd in (0x52, 0x53) and 0x30 <= args[0] <= 0xB6 and cmd == 0x53:
            assert args[0] & 3 != 2, "FM channel 6 register write"
        elif cmd == 0x52 and args[0] == 0x2B:
            raise AssertionError("DAC enable belongs to the XGM2 driver")
    assert seen <= {0x50, 0x52, 0x53, 0x61, 0x62, 0x63, 0x66} | set(range(0x70, 0x80))
    assert {0x50, 0x52, 0x53} <= seen and waited == struct.unpack_from("<I", vgm, 0x18)[0]


def test_deterministic(song1):
    log, vgm = song1
    assert music.convert(log)[0] == vgm
