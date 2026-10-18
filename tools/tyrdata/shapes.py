"""Graphics: background tiles shapes?.dat, Sprite2 sheets (newsh*.shp),
Sprite tables (tyrian.shp 0-6) and tyrian.pic (docs/data_formats.md §1.5, §3)."""
from . import data_path
from .binfile import Reader, offset_table

TILE_W, TILE_H = 24, 28
TILE_RECORDS = 600
SPRITE2_W, SPRITE2_H = 12, 14
TRANSPARENT = None


def load_tiles(name):
    """shapes?.dat -> list of 600 records: None (blank) or 672-byte pixels."""
    with open(data_path(name), "rb") as f:
        r = Reader(f.read())
    return [None if r.u8() else r.take(TILE_W * TILE_H) for _ in range(TILE_RECORDS)]


def decode_sprite2(sheet, index):
    """blit_sprite2: 1-based sprite `index` of a raw sheet -> 14 rows of 12
    pixels (None = transparent).  The cursor is relative: a row break moves
    down one line and back 12 pixels, keeping any x offset."""
    off = sheet[(index - 1) * 2] | sheet[(index - 1) * 2 + 1] << 8
    rows = [[TRANSPARENT] * SPRITE2_W for _ in range(SPRITE2_H)]
    x = y = 0
    p = off
    while p < len(sheet):
        b = sheet[p]
        p += 1
        if b == 0x0F:
            break
        x += b & 0x0F
        n = b >> 4
        if n == 0:                    # next row: pixels += pitch - 12
            x, y = x - SPRITE2_W, y + 1
            continue
        for _ in range(n):
            if 0 <= y < SPRITE2_H and 0 <= x < SPRITE2_W:
                rows[y][x] = sheet[p]
            p, x = p + 1, x + 1
    return rows


def sprite2_count(sheet):
    return (sheet[0] | sheet[1] << 8) // 2


def load_sheet(name):
    with open(data_path(name), "rb") as f:
        return f.read()


def decode_sprite_table(data, start=0):
    """Sprite table (sprite.c:load_sprites/blit_sprite) -> list of
    None or rows (lists of pixels, None = transparent)."""
    r = Reader(data, start)
    out = []
    for _ in range(r.u16()):
        if not r.u8():
            out.append(None)
            continue
        w, h, size = r.u16(), r.u16(), r.u16()
        stream = r.take(size)
        rows = [[TRANSPARENT] * w for _ in range(h)]
        x = y = i = 0
        while i < len(stream):
            b = stream[i]
            i += 1
            if b == 255:                  # skip n transparent pixels
                x += stream[i]
                i += 1
            elif b == 254:                # next row
                x = w
            elif b == 253:                # 1 transparent pixel
                x += 1
            else:
                if y >= h:
                    break
                rows[y][x] = b
                x += 1
            if x >= w:                    # overflow past the row is dropped
                x, y = 0, y + 1
        out.append(rows)
    return out


def main_shape_tables():
    """tyrian.shp: list of 12 raw segments (tables 0-6 Sprite, 7-11 Sprite2)."""
    data = load_sheet("tyrian.shp")
    return [data[a:b] for a, b in offset_table(data)]


PCX_PALETTE = (0, 7, 5, 8, 10, 5, 18, 19, 19, 20, 21, 22, 5)   # pcxmast.c:pcxpal


def load_pic(num):
    """tyrian.pic #num (1-based) -> (64000 palette indices, palette number)."""
    data = load_sheet("tyrian.pic")
    a, b = offset_table(data)[num - 1]
    out = bytearray()
    i = a
    while len(out) < 320 * 200:
        c = data[i]
        i += 1
        if c >= 0xC0:
            out += bytes([data[i]]) * (c & 0x3F)
            i += 1
        else:
            out.append(c)
    return bytes(out[:64000]), PCX_PALETTE[num - 1]
