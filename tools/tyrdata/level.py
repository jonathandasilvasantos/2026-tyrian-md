"""Level files tyrian1..4.lvl and episode scripts levels1..4.dat
(docs/data_formats.md §1-§2, tyrian2.c:JE_loadMap)."""
from dataclasses import dataclass

from . import data_path
from .binfile import Reader, offset_table, read_encrypted_strings

MAP_BLOCK_SIZE = 22368
LAYER_DIMS = ((14, 300), (14, 600), (15, 600))   # (columns, rows) of layers 1..3


@dataclass
class Event:
    time: int
    type: int
    dat: int
    dat2: int
    dat3: int
    dat4: int
    dat5: int
    dat6: int


@dataclass
class Level:
    shape_file: str            # tile file name, shapes?.dat
    map_x: tuple               # (mapX, mapX2, mapX3)
    enemies: list              # levelEnemy[] ids for random spawns
    events: list               # [Event]
    map_sh: list               # 3 x 128 local index -> shapes record (1-based, 0 = none)
    layers: list               # 3 x rows x cols local indexes


def read_level(data, start):
    r = Reader(data, start)
    r.u8()                                  # char_mapFile: ignored by the engine
    shape_char = chr(r.u8()).lower()
    map_x = (r.u16(), r.u16(), r.u16())
    enemies = r.array(r.u16, r.u16())
    events = []
    for _ in range(r.u16()):
        # on-disk order: dat3, dat5, dat6, dat4 (differs from the C struct)
        time, typ, dat, dat2 = r.u16(), r.u8(), r.s16(), r.s16()
        dat3, dat5, dat6, dat4 = r.s8(), r.s8(), r.s8(), r.u8()
        events.append(Event(time, typ, dat, dat2, dat3, dat4, dat5, dat6))
    map_start = r.pos
    map_sh = [r.array(r.u16be, 128) for _ in range(3)]
    layers = [[list(r.take(cols)) for _ in range(rows)] for cols, rows in LAYER_DIMS]
    assert r.pos - map_start == MAP_BLOCK_SIZE
    return Level(f"shapes{shape_char}.dat", map_x, enemies, events, map_sh, layers)


def lvl_entries(episode):
    with open(data_path(f"tyrian{episode}.lvl"), "rb") as f:
        data = f.read()
    return data, offset_table(data)


def level_count(episode):
    _, ents = lvl_entries(episode)
    return len(ents) // 2          # the odd last entry is EOF / episode-4 item data


def load_level(episode, lvl_file_num):
    """Level by the 1-based `lvlFileNum` of the ]L script command."""
    data, ents = lvl_entries(episode)
    return read_level(data, ents[(lvl_file_num - 1) * 2][0])


def load_script(episode):
    """levels?.dat as decrypted lines."""
    with open(data_path(f"levels{episode}.dat"), "rb") as f:
        return read_encrypted_strings(f.read())


@dataclass
class LevelCmd:
    next_level: int
    name: str
    song: int
    lvl_file_num: int
    bonus: bool


def parse_level_cmd(line):
    """`]L[ 9999 004 TYRIAN   18 09`: fixed columns, atoi semantics."""
    def atoi(s):
        s = s.strip()
        n = ""
        for ch in s:
            if ch.isdigit() or (not n and ch == "-"):
                n += ch
            else:
                break
        return int(n) if n not in ("", "-") else 0
    return LevelCmd(atoi(line[9:]), line[13:22].rstrip(), atoi(line[22:]), atoi(line[25:]),
                    line[27:28] == "$" or line[28:29] == "$")
