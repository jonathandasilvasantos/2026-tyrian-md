"""Episode scripts (levels?.dat) compiled to a small bytecode for the ROM
(docs/data_formats.md §2, tyrian2.c:JE_loadMap).

Each '*' line opens a section (1-based).  Only commands with gameplay
effect are kept; text/picture/music screens are dropped for now
(agents.md §8).
"""
import struct

from . import data_path
from .binfile import read_encrypted_strings
from .level import load_script

OP_END, OP_JUMP, OP_LEVEL, OP_SHOP, OP_CHOOSE = 0, 1, 2, 3, 4
OP_SKIP_IF_HARD, OP_JUMP_IF_EASY, OP_SAVE, OP_JUMP_IF_DIED, OP_JUMP_IF_TIMER = 5, 6, 7, 8, 9
OP_JUMP_IF_SHIP13, OP_END_EPISODE, OP_SHOP_SONG = 10, 11, 12

SHOP_ROWS = ("Ship", "WeapF", "WeapR", "Power", "Engine", "Opt1", "Opt2", "Armor", "Shield")


def atoi(s):
    s = s.lstrip()
    n = ""
    for ch in s:
        if ch.isdigit() or (not n and ch == "-"):
            n += ch
        else:
            break
    return int(n) if n not in ("", "-") else 0


def help_section(title_prefix):
    """Entries of a tyrian.hdt help text section (helptext.c)."""
    data = open(data_path("tyrian.hdt"), "rb").read()
    pos = struct.unpack("<I", data[:4])[0]
    s = read_encrypted_strings(data, 4, pos)
    start = next(i for i, l in enumerate(s) if l.startswith(title_prefix))
    out = []
    for l in s[start + 1:]:
        if not l:
            break
        out.append(l)
    return out


def planet_names():
    """tyrian.hdt help text, section 3: planet n -> name."""
    data = open(data_path("tyrian.hdt"), "rb").read()
    pos = struct.unpack("<I", data[:4])[0]
    s = read_encrypted_strings(data, 4, pos)
    start = next(i for i, l in enumerate(s) if l.startswith("** Section 3"))
    names = []
    for l in s[start + 1:]:
        if not l:
            break
        names.append(l)
    return names


def compile_script(episode):
    """-> (ops bytes, section offsets [1..N], shops [9 lists], names [str])"""
    lines = load_script(episode)
    ops, sections, shops, names = bytearray(), [0], [], []
    i = 0
    while i < len(lines):
        s = lines[i]
        i += 1
        if s.startswith("*"):
            sections.append(len(ops))
            continue
        if not s.startswith("]") or len(s) < 2:
            continue
        c = s[1]
        if c == "J":
            ops += bytes([OP_JUMP, atoi(s[3:])])
        elif c == "L":
            nxt = atoi(s[9:])
            name = s[13:22].rstrip()
            flags = (1 if s[28:29] == "$" else 0) | (2 if s[27:28] == "$" else 0)
            if name not in names:
                names.append(name)
            ops += bytes([OP_LEVEL, nxt, atoi(s[25:]) - 1, atoi(s[22:]), flags, names.index(name)])
        elif c == "I":
            rows = []
            for _ in range(9):
                t = lines[i] if i < len(lines) else ""
                i += 1
                rows.append([int(x) for x in t[8:].split()] if len(t) > 8 else [])
            ops += bytes([OP_SHOP, len(shops)])
            shops.append(rows)
        elif c == "G":
            n = atoi(s[7:])
            pairs = [(atoi(s[1 + 8 * k:]), atoi(s[4 + 8 * k:])) for k in range(1, n + 1)]
            ops += bytes([OP_CHOOSE, n] + [v for p in pairs for v in p])
        elif c == "h":
            ops += bytes([OP_SKIP_IF_HARD])
        elif c == "H":
            ops += bytes([OP_JUMP_IF_EASY, atoi(s[4:])])
        elif c in "sb":
            ops += bytes([OP_SAVE])
        elif c == "l":
            ops += bytes([OP_JUMP_IF_DIED, atoi(s[3:])])
        elif c == "t":
            ops += bytes([OP_JUMP_IF_TIMER, atoi(s[3:])])
        elif c == "w":
            ops += bytes([OP_JUMP_IF_SHIP13, atoi(s[3:])])
        elif c == "i":
            ops += bytes([OP_SHOP_SONG, atoi(s[3:])])
        elif c == "Q":
            ops += bytes([OP_END_EPISODE])
        # ]2 (two-player jump) never taken; W P M U V R C B F A g x e ? ! + @ S n: screens/extras
    ops += bytes([OP_END])
    return bytes(ops), sections, shops, names
