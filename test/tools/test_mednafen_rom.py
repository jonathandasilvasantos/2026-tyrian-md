"""The emulator compatibility copy must never change the hardware ROM."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from mednafen_rom import prepare  # noqa: E402


def test_only_product_code_changes_in_copy(tmp_path):
    source, destination = tmp_path / "rom.bin", tmp_path / "launch.bin"
    data = bytearray(1024)
    data[0x100:0x110] = b"SEGA SSF        "
    source.write_bytes(data)
    assert prepare(source, destination) == str(destination)
    copied = destination.read_bytes()
    assert source.read_bytes() == data
    assert copied[:0x180] == data[:0x180] and copied[0x18E:] == data[0x18E:]
    assert copied[0x180:0x18E] == b"GM T-12056 -00"
    assert len(copied) == len(data)
    with pytest.raises(ValueError):
        prepare(source, source)


def test_linear_rom_does_not_need_a_copy(tmp_path):
    source, destination = tmp_path / "rom.bin", tmp_path / "launch.bin"
    source.write_bytes(bytes(1024))
    assert prepare(source, destination) == str(source)
    assert not destination.exists()
