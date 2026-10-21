#!/usr/bin/env python3
"""gen.py — convert tyrian-data/ into src/gen_data/ (agents.md §2).

Usage: python3 tools/gen.py [out_dir] [--episode 1..4]   (default src/gen_data)
Outputs are byte-identical for identical inputs.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import numpy as np  # noqa: E402

from tyrdata import difficulty as difficulty_mod
from tyrdata import filters as filters_mod
from tyrdata import script as script_mod  # noqa: E402
from tyrdata import data_path, emit, items, level, palette, quantize, raster, shapes  # noqa: E402

BG_LAYERS = 2              # layer 3 (top) is not converted yet (agents.md §8)
GAME_PALETTE = 5           # in-game PC palette (docs/data_formats.md §5)
# Canonical PAL2 from episode-1 ship/weapon/option quantization. Pin it for
# the whole campaign: enemies and backgrounds must never tint player shots.
PLAYER_CRAM = (0x0A00, 0x00A0, 0x0002, 0x0222, 0x0C42, 0x0444, 0x0886,
               0x02C6, 0x0008, 0x0CC8, 0x002C, 0x048C, 0x0CEC)
BG_WIN_ROWS = 30           # cell rows resident per layer (src/bg.c)


def gen_filters(out):
    inverse, colors = filters_mod.make_tables(palette.load_palettes()[GAME_PALETTE])
    emit.write_if_changed(os.path.join(out, "filters.h"), emit.HEADER +
                         '#ifndef GEN_FILTERS_H\n#define GEN_FILTERS_H\n#include <types.h>\n'
                         'extern const u8 cram_pc_index[512];\nextern const u16 filtered_palette[256];\n#endif\n')
    emit.write_if_changed(os.path.join(out, "filters.c"), emit.HEADER + '#include "filters.h"\n' +
                         emit.c_array("u8", "cram_pc_index", inverse, 16, "{}") +
                         emit.c_array("u16", "filtered_palette", colors, 16, "0x{:04X}"))


def gen_palettes(out):
    pals = palette.load_palettes()
    words = [palette.to_cram(rgb) for p in pals for rgb in p]
    h = (emit.HEADER + "#ifndef GEN_PALETTES_H\n#define GEN_PALETTES_H\n#include <types.h>\n"
         f"#define PAL_COUNT {len(pals)}\n"
         "/* PC palette p, color i -> CRAM word: tyr_palettes[p * 256 + i] */\n"
         "extern const u16 tyr_palettes[PAL_COUNT * 256];\n#endif\n")
    c = emit.HEADER + '#include "palettes.h"\n' + emit.c_array("u16", "tyr_palettes", words)
    emit.write_if_changed(os.path.join(out, "palettes.h"), h)
    emit.write_if_changed(os.path.join(out, "palettes.c"), c)


def gen_level_bg(ep, num, idx):
    """Quantize layers 1-2 of a level onto palette lines 0-1 and return them
    (C source, stats, palettes) as cells + metatiles for the streaming
    background engine (src/bg.c)."""
    lvl, bmps = raster.load_level_bitmaps(ep, num)
    bmps = bmps[:BG_LAYERS]
    rgb = palette.load_palettes()[GAME_PALETTE]
    keys, hists, where = {}, [], []
    for li, bmp in enumerate(bmps):
        cs = raster.cells(bmp)
        ids = []
        for c in cs:
            k = (li, c.tobytes())
            if k not in keys:
                keys[k] = len(hists)
                hists.append({})
            i = keys[k]
            vals, cnt = np.unique(c, return_counts=True)
            for v, n in zip(vals, cnt):
                if li == 0 or v != raster.TRANSPARENT:
                    hists[i][int(v)] = hists[i].get(int(v), 0) + int(n)
            ids.append(i)
        where.append(ids)
    pals, assign = quantize.quantize_cells(hists, rgb)
    lut = [quantize.remap(np.arange(256), rgb, p) for p in pals]   # PC color -> 1..15
    # Per layer: deduplicated 4bpp cells (H/V flips merged) and metatiles.
    # A metatile is one map column of 2 map rows (24x56 px = 3x7 cells):
    # 28-px map rows only realign with the 8-px cell grid every 2 rows.
    layers = []
    for li, bmp in enumerate(bmps):
        h, w = bmp.shape
        ccols = w // 8
        cell_ids, cell_data, star_clear = {}, [], []
        attrs = np.zeros((h // 8, ccols), np.uint16)
        for n, i in enumerate(where[li]):
            cy, cx = divmod(n, ccols)
            g = int(assign[i])
            blk = bmp[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8]
            q = lut[g][blk].astype(np.uint8)
            if li > 0:
                q[blk == raster.TRANSPARENT] = 0
            forms = [(q, 0), (q[:, ::-1], 0x800), (q[::-1, :], 0x1000), (q[::-1, ::-1], 0x1800)]
            key, flip = min((f.tobytes(), fl) for f, fl in forms)
            if key not in cell_ids:
                cell_ids[key] = len(cell_data)
                cell_data.append(key)
                star_clear.append(1)
            star_clear[cell_ids[key]] &= int(np.all(blk == (0 if li == 0 else raster.TRANSPARENT)))
            attrs[cy, cx] = cell_ids[key] | flip | (g << 13)
        meta_ids, metas = {}, []
        mrows, mcols = h // 56, w // 24
        metamap = np.zeros((mrows, mcols), np.uint16)
        for my in range(mrows):
            for mx in range(mcols):
                m = attrs[my * 7:my * 7 + 7, mx * 3:mx * 3 + 3].tobytes()
                if m not in meta_ids:
                    meta_ids[m] = len(metas)
                    metas.append(m)
                metamap[my, mx] = meta_ids[m]
        tiles = []
        for key in cell_data:
            px = np.frombuffer(key, np.uint8).reshape(8, 8)
            for row in px:
                v = 0
                for p in row:
                    v = (v << 4) | int(p)
                tiles.append(v)
        meta_words = [int(x) for m in metas for x in np.frombuffer(m, np.uint16)]
        # worst number of distinct cells resident at once in src/bg.c's
        # BG_WIN_ROWS-row window: sizes its VRAM slot pool
        ids = attrs & 0x7FF
        need = max(len(np.unique(ids[y:y + BG_WIN_ROWS])) for y in range(max(1, len(ids) - BG_WIN_ROWS + 1)))
        layers.append(dict(cells=len(cell_data), tiles=tiles, meta=meta_words, star_clear=star_clear,
                           metamap=[int(x) for x in metamap.ravel()], mrows=mrows, mcols=mcols,
                           need=need))
    cram = []
    for p in pals:
        words = [0] + [palette.to_cram(tuple(int(x) for x in c)) for c in p]
        cram += words + [0] * (16 - len(words))
    pre = f"lv{idx}_"
    c = emit.c_array("u16", pre + "palette", cram)
    for li, L in enumerate(layers):
        c += emit.tile_array(f"{pre}bg{li + 1}_tiles", L["tiles"])
        c += emit.c_array("u8", f"{pre}bg{li + 1}_clear", L["star_clear"], 32, "{}")
        c += emit.c_array("u16", f"{pre}bg{li + 1}_meta", L["meta"], 21)
        c += emit.c_array("u16", f"{pre}bg{li + 1}_metamap", L["metamap"], L["mcols"], "{}")
    entry = ("    { { " + ", ".join(
        f"{{ {pre}bg{li + 1}_tiles, {pre}bg{li + 1}_meta, {pre}bg{li + 1}_metamap, {pre}bg{li + 1}_clear, {L['cells']}, {L['mrows']}, {L['mcols']} }}"
        for li, L in enumerate(layers)) + f" }}, {pre}palette, {sum(L['need'] for L in layers)}, "
             f"{sum(L['cells'] for L in layers)} }}")
    stats = ", ".join(f"L{li + 1} {L['cells']} cells {L['need']} resident" for li, L in enumerate(layers))
    print(f"   gen: level {ep}.{num} bg: {stats}")
    return dict(c=c, entry=entry, pals=pals, need=sum(L['need'] for L in layers),
                cells=sum(L['cells'] for L in layers))


def write_level_bgs(out, bgs):
    h = (emit.HEADER + "#ifndef GEN_LEVEL_BG_H\n#define GEN_LEVEL_BG_H\n#include <types.h>\n"
         "/* Per level: layers 1-2 on palette lines 0-1.  Metatile = 3x7 cells (one\n"
         " * map column x 2 map rows); entries are VDP tile attributes whose index\n"
         " * field is the layer cell number. */\n"
         "typedef struct {\n    const u32 *tiles;      /* 8 longs per cell */\n"
         "    const u16 *meta;       /* 21 attrs per metatile, row-major */\n"
         "    const u16 *metamap;    /* mrows x mcols metatile ids, top row first */\n"
         "    const u8 *star_clear; /* fully clear cells for starfield occlusion */\n"
         "    u16 cells, mrows, mcols;\n} BgLayer;\n"
         f"#define BG_LAYER_COUNT {BG_LAYERS}\n"
         "typedef struct {\n    BgLayer layers[BG_LAYER_COUNT];\n    const u16 *palette;   /* 32 CRAM words */\n"
         "    u16 pool_need, cells_total;\n} LevelBg;\n"
         f"#define BG_WIN_ROWS {BG_WIN_ROWS}\n"
         "/* worst over the levels: VRAM slot pool and per-cell tables */\n"
         f"#define BG_POOL_NEED {max(b['need'] for b in bgs)}\n"
         f"#define BG_CELLS_TOTAL {max(b['cells'] for b in bgs)}\n"
         "extern const LevelBg level_bgs[];\n#endif\n")
    c = emit.HEADER + '#include "level_bg.h"\n' + "".join(b["c"] for b in bgs)
    c += "const LevelBg level_bgs[] = {\n" + ",\n".join(b["entry"] for b in bgs) + "\n};\n"
    emit.write_if_changed(os.path.join(out, "level_bg.h"), h)
    emit.write_if_changed(os.path.join(out, "level_bg.c"), c)


def gen_levels(out, ep, nums):
    """Level headers and events (docs/data_formats.md §1.2-1.3) as portable C;
    levels[i] is lvlFileNum i + 1 of the episode."""
    h = (emit.HEADER + "#ifndef GEN_LEVEL_H\n#define GEN_LEVEL_H\n#include \"game/types.h\"\n"
         "typedef struct {\n    uint16_t time;\n    uint8_t type, dat4;\n    int16_t dat, dat2;\n"
         "    int8_t dat3, dat5, dat6;\n} LevelEvent;\n"
         "typedef struct {\n"
         "    const LevelEvent *events;    /* + a sentinel at time 65500 (JE_loadMap) */\n"
         "    const uint16_t *enemies;     /* levelEnemy[] */\n"
         "    uint16_t event_count;\n    uint8_t enemy_max;\n    uint8_t map_x, map_x3;\n"
         "} LevelDef;\n"
         f"#define LEVEL_COUNT {len(nums)}\n"
         f"#define LEVEL_EPISODE {ep}\n"
         f"#define LEVEL_FIRST {script_mod.first_level(ep)}\n"
         "extern const LevelDef levels[LEVEL_COUNT];\n#endif\n")
    c = emit.HEADER + '#include "level.h"\n'
    entries = []
    for i, num in enumerate(nums):
        lvl = level.load_level(ep, num)
        c += f"static const uint16_t lv{i}_enemies[] = {{" + ",".join(str(x) for x in lvl.enemies + [0]) + "};\n"
        evs = lvl.events + [level.Event(65500, 0, 0, 0, 0, 0, 0, 0)]
        c += f"static const LevelEvent lv{i}_events[] = {{\n    " + ",\n    ".join(
            "{%d,%d,%d,%d,%d,%d,%d,%d}" % (e.time, e.type, e.dat4, e.dat, e.dat2, e.dat3, e.dat5, e.dat6)
            for e in evs) + "\n};\n"
        entries.append(f"    {{ lv{i}_events, lv{i}_enemies, {len(lvl.events)}, {len(lvl.enemies)}, "
                       f"{lvl.map_x[0]}, {lvl.map_x[2]} }}")
    c += "const LevelDef levels[LEVEL_COUNT] = {\n" + ",\n".join(entries) + "\n};\n"
    emit.write_if_changed(os.path.join(out, "level.h"), h)
    emit.write_if_changed(os.path.join(out, "level.c"), c)


def gen_script(out, ep):
    """Episode script bytecode (tools/tyrdata/script.py) as portable C."""
    ops, sections, shops, names, _ = script_mod.compile_script(ep)
    planets = script_mod.planet_names()
    h = (emit.HEADER + "#ifndef GEN_SCRIPT_H\n#define GEN_SCRIPT_H\n#include \"game/types.h\"\n"
         "/* opcodes: see tools/tyrdata/script.py */\n"
         + "".join(f"#define {n} {v}\n" for n, v in vars(script_mod).items() if n.startswith("OP_"))
         + f"#define SCRIPT_SECTIONS {len(sections) - 1}\n#define SHOP_COUNT {len(shops)}\n"
         "#define SHOP_ROWS 9   /* Ship WeapF WeapR Power Engine Opt1 Opt2 Armor Shield */\n"
         "#define SHOP_ROW_MAX 8\n"
         "typedef struct {\n    uint8_t count[SHOP_ROWS];\n    uint8_t items[SHOP_ROWS][SHOP_ROW_MAX];\n} Shop;\n"
         "extern const uint8_t script_ops[];\n"
         "extern const uint16_t script_sections[SCRIPT_SECTIONS + 1];   /* [section] -> op offset */\n"
         "extern const Shop shops[SHOP_COUNT];\n"
         "extern const char *const level_names[];\n"
         f"#define PLANET_COUNT {len(planets)}\n"
         "extern const char *const planet_names[PLANET_COUNT + 1];   /* 1-based */\n"
         "extern const char *const window_texts[9];   /* event 16 (helptext.c outputs) */\n#endif\n")
    c = emit.HEADER + '#include "script.h"\n'
    c += emit.c_array("uint8_t", "script_ops", list(ops), 16, "{}")
    c += emit.c_array("uint16_t", "script_sections", sections, 16, "{}")
    rows = []
    for sh in shops:
        cnt = [min(len(r), 8) for r in sh]
        its = ["{" + ",".join(str(x) for x in (r[:8] + [0] * (8 - len(r[:8])))) + "}" for r in sh]
        rows.append("{{" + ",".join(map(str, cnt)) + "},{" + ",".join(its) + "}}")
    c += "const Shop shops[SHOP_COUNT] = {\n    " + ",\n    ".join(rows) + "\n};\n"
    def cstr(x):
        return '"' + x.replace("\\", "\\\\").replace('"', '\\"') + '"'
    c += "const char *const level_names[] = {" + ",".join(cstr(n) for n in names) + "};\n"
    c += "const char *const planet_names[PLANET_COUNT + 1] = {\"\"," + ",".join(cstr(n) for n in planets) + "};\n"
    outs = (script_mod.help_section("** Section 8") + [""] * 9)[:9]
    c += "const char *const window_texts[9] = {" + ",".join(cstr(n) for n in outs) + "};\n"
    emit.write_if_changed(os.path.join(out, "script.h"), h)
    emit.write_if_changed(os.path.join(out, "script.c"), c)
    print(f"   gen: episode {ep} script: {len(sections) - 1} sections, {len(ops)} bytes, {len(shops)} shops")


def _glyph_tiles(glyph, cells_h, fg=15, bg=14, threshold=0):
    """A Sprite-table glyph (rows of indices/None) -> 8 x (8*cells_h) cell
    tiles, drawn at (0, 1) in fg on an opaque bg.  Font pixels are shade
    offsets added to a color bank (JE_dString); shades <= threshold are the
    dark anti-aliasing that shapes the small glyphs and stay background."""
    img = np.full((8 * cells_h, 8), bg, np.int32)
    if glyph and len(glyph[0]) > 8:   # M, W (12 px): squeeze to the 8-px cell
        w = len(glyph[0])
        glyph = [[row[x * w // 8] for x in range(8)] for row in glyph]
    if glyph:
        for y, row in enumerate(glyph[:8 * cells_h - 1]):
            for x, p in enumerate(row[:8]):
                if p is not None and p > threshold:
                    img[y + 1, x] = fg
    return tiles_column_major(img, 1, cells_h)


def gen_fonts(out):
    """Tyrian's own fonts as VDP tiles, palette line 2 (14 black, 15 white):
    TINY_FONT (4x6) one cell per char for the HUD, SMALL_FONT (8x10) two
    cells per char for the menus; ASCII 32..127 (fonthand.c:fontMap)."""
    tabs = shapes.main_shape_tables()
    tiny = shapes.decode_sprite_table(tabs[2])
    small = shapes.decode_sprite_table(tabs[1])
    font_map = [-1] * 33 + [26, 33, 60, 61, 62, -1, 32, 64, 65, 63, 84, 29, 83, 28, 80,
                            79, 70, 71, 72, 73, 74, 75, 76, 77, 78, 31, 30, -1, 85, -1, 27,
                            -1] + list(range(0, 26)) + [68, 82, 69, -1, -1, -1] + list(range(34, 60)) + [66, 81, 67, -1, -1]
    hud, menu = [], []
    for ch in range(32, 128):
        gi = font_map[ch] if ch < len(font_map) else -1
        hud += _glyph_tiles(tiny[gi] if 0 <= gi < len(tiny) else None, 1, threshold=4)
        menu += _glyph_tiles(small[gi] if 0 <= gi < len(small) else None, 2)
    # HUD bar tiles: fill 0..8 px from the left, in colors 1..3 of hud_bar_colors
    bars = []
    for color in (13, 12, 11):
        for fill in range(9):
            img = np.full((8, 8), 14, np.int32)
            img[2:6, :fill] = color
            bars += tiles_column_major(img, 1, 1)
    h = (emit.HEADER + "#ifndef GEN_FONTS_H\n#define GEN_FONTS_H\n#include <types.h>\n"
         "/* ASCII 32..127, palette line 2: HUD 1 tile per char, menus 2 tiles\n"
         " * (top, bottom) per char; HUD bars: 3 colors x fill 0..8 px */\n"
         "extern const u32 hud_font_tiles[96 * 8];\n"
         "extern const u32 menu_font_tiles[96 * 16];\n"
         "extern const u32 hud_bar_tiles[27 * 8];\n#endif\n")
    c = emit.HEADER + '#include "fonts.h"\n'
    c += emit.c_array("u32", "hud_font_tiles", hud, 8, "0x{:08X}")
    c += emit.c_array("u32", "menu_font_tiles", menu, 8, "0x{:08X}")
    c += emit.c_array("u32", "hud_bar_tiles", bars, 8, "0x{:08X}")
    emit.write_if_changed(os.path.join(out, "fonts.h"), h)
    emit.write_if_changed(os.path.join(out, "fonts.c"), c)


def gen_sounds(out, res_dir):
    """tyrian.snd (29 effects) and voices.snd (9 voices, last 100 bytes cut
    as nortsong.c:loadSndFile does): signed 8-bit 11025 Hz mono ->
    WAV files for rescomp (XGM2 driver), all at 6.65 kHz (half rate: the
    PC's 11 kHz samples at 13.3 kHz cost 0.17 MB more ROM)."""
    import wave
    from tyrdata.binfile import offset_table
    os.makedirs(os.path.join(res_dir, "sfx"), exist_ok=True)
    entries = []
    for fname, trim, half in (("tyrian.snd", 0, True), ("voices.snd", 100, True)):
        data = open(data_path(fname), "rb").read()
        for a, b in offset_table(data):
            pcm = bytes((x + 128) & 0xFF for x in data[a:b - trim])
            name = f"snd_{len(entries) + 1:02d}"
            path = os.path.join(res_dir, "sfx", name + ".wav")
            buf = __import__("io").BytesIO()
            with wave.open(buf, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(1)
                w.setframerate(11025)
                w.writeframes(pcm)
            if not os.path.exists(path) or open(path, "rb").read() != buf.getvalue():
                open(path, "wb").write(buf.getvalue())
            entries.append((name, half))
    res = "".join(f'WAV {n} "sfx/{n}.wav" XGM2 {6650 if half else 13300}\n' for n, half in entries)
    h = (emit.HEADER + "#ifndef GEN_SOUND_TABLE_H\n#define GEN_SOUND_TABLE_H\n#include <genesis.h>\n"
         f"#define SOUND_COUNT {len(entries)}   /* ids 1..SOUND_COUNT (sndmast.h) */\n"
         "typedef struct {\n    const u8 *data;\n    u32 len;\n    bool half_rate;\n} SoundDef;\n"
         "extern const SoundDef sound_defs[SOUND_COUNT + 1];\n#endif\n")
    c = (emit.HEADER + '#include "sound_table.h"\n#include "resources.h"\n'
         "const SoundDef sound_defs[SOUND_COUNT + 1] = {\n    { 0, 0, FALSE },\n"
         + "".join(f"    {{ {n}, sizeof({n}), {'TRUE' if half else 'FALSE'} }},\n" for n, half in entries) + "};\n")
    emit.write_if_changed(os.path.join(out, "sound_table.h"), h)
    emit.write_if_changed(os.path.join(out, "sound_table.c"), c)
    return res


def gen_title(out, art=os.path.join("res", "art", "title_md.png")):
    """Title screen art (res/art/title_md.png: the original title screen
    redrawn with "MD", made with Gemini image generation from the PC title)
    -> 320x224, quantized per cell to palette lines 0, 1 and 3 (line 2 is
    the menu font), deduplicated cells and a 40x28 tilemap."""
    from PIL import Image
    im = Image.open(art).convert("RGB")
    w, h = im.size
    im = im.resize((320, round(320 * h / w)), Image.LANCZOS)
    top = (im.size[1] - 224) // 2
    im = im.crop((0, top, 320, top + 224))
    # first to 256 colors so the cell quantizer works on an indexed image
    pim = im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    rgb = np.array(pim.getpalette()[:768], np.float64).reshape(256, 3)
    bmp = np.array(pim, np.uint8)
    cs = raster.cells(bmp)
    hists = []
    for c in cs:
        v, n = np.unique(c, return_counts=True)
        hists.append({int(a): int(b) for a, b in zip(v, n)})
    pals, assign = quantize.quantize_cells(hists, rgb, n_pals=3)
    luts = [quantize.remap(np.arange(256), rgb, p) for p in pals]
    lines = (0, 1, 3)
    cell_ids, tiles, tmap = {}, [], []
    for c, g in zip(cs, assign):
        q = luts[g][c].astype(np.uint8)
        forms = [(q, 0), (q[:, ::-1], 0x800), (q[::-1, :], 0x1000), (q[::-1, ::-1], 0x1800)]
        key, flip = min((f.tobytes(), fl) for f, fl in forms)
        if key not in cell_ids:
            cell_ids[key] = len(cell_ids)
            tiles += tiles_column_major(np.frombuffer(key, np.uint8).reshape(8, 8), 1, 1)
        tmap.append(cell_ids[key] | flip | (lines[int(g)] << 13))
    cram = [0] * 64
    for p, line in zip(pals, lines):
        wds = [0] + [palette.to_cram(tuple(int(x) for x in c)) for c in p]
        cram[line * 16:line * 16 + len(wds)] = wds
    h = (emit.HEADER + "#ifndef GEN_TITLE_H\n#define GEN_TITLE_H\n#include <types.h>\n"
         f"#define TITLE_TILES {len(cell_ids)}\n"
         "/* 40x28 tilemap: attrs with the tile index relative to the loaded tiles;\n"
         " * palette: 64 CRAM words, line 2 unused (menu font) */\n"
         "extern const u32 title_tiles[];\nextern const u16 title_map[40 * 28];\n"
         "extern const u16 title_palette[64];\n#endif\n")
    c = emit.HEADER + '#include "title.h"\n'
    c += emit.c_array("u32", "title_tiles", tiles, 8, "0x{:08X}")
    c += emit.c_array("u16", "title_map", tmap, 40)
    c += emit.c_array("u16", "title_palette", cram)
    emit.write_if_changed(os.path.join(out, "title.h"), h)
    emit.write_if_changed(os.path.join(out, "title.c"), c)
    print(f"   gen: title: {len(cell_ids)} cells")


def gen_items(out, episode=1):
    """Weapon, port, ship and generator tables (docs/data_formats.md §4) as
    portable C (src/game uses them on the ROM and on the host)."""
    it = items.load_items(episode)
    h = (emit.HEADER + "#ifndef GEN_ITEMS_H\n#define GEN_ITEMS_H\n#include \"game/types.h\"\n"
         "typedef struct {\n"
         "    uint8_t shotrepeat, multi, weapani, max, tx, ty, aim;\n"
         "    uint8_t attack[8], del[8];\n"
         "    int8_t sx[8], sy[8], bx[8], by[8];\n"
         "    uint16_t sg[8];\n"
         "    int8_t acceleration, accelerationx;\n"
         "    uint8_t circlesize, sound, trail, shipblastfilter;\n"
         "} Weapon;\n"
         "typedef struct {\n    uint16_t op[2][11];\n    uint16_t poweruse;\n    uint8_t opnum;\n} WeaponPort;\n"
         "typedef struct {\n    uint16_t shipgraphic;\n    int8_t spd;\n    uint8_t dmg, ani;\n} Ship;\n"
         "typedef struct {\n    uint8_t tpwr, mpwr;\n} Shield;\n"
         "typedef struct { uint16_t weapon; uint8_t power, type; } Special;\n"
         "typedef struct {\n    uint16_t graphic[20], weapon;\n"
         "    uint8_t power, style, animation, frames, ammo, port;\n} Option;\n"
         "typedef struct {\n"
         "    uint8_t ani, tur[3], freq[3];\n"
         "    int8_t xmove, ymove, xaccel, yaccel, xcaccel, ycaccel;\n"
         "    int16_t startx, starty;\n"
         "    int8_t startxc, startyc;\n"
         "    uint8_t armor, esize;\n"
         "    uint16_t egraphic[20];\n"
         "    uint8_t explosiontype, animate, shapebank;\n"
         "    int8_t xrev, yrev;\n"
         "    uint16_t dgr;\n"
         "    int8_t dlevel, dani;\n"
         "    uint8_t elaunchfreq;\n"
         "    uint16_t elaunchtype;\n"
         "    int16_t value;\n"
         "    uint16_t eenemydie;\n"
         "} EnemyDat;\n"
         f"#define WEAP_NUM {len(it['weapons']) - 1}\n#define PORT_NUM {len(it['ports']) - 1}\n"
         f"#define SHIP_NUM {len(it['ships']) - 1}\n#define POWER_NUM {len(it['powers']) - 1}\n"
         f"#define OPTION_NUM {len(it['options']) - 1}\n#define SHIELD_NUM {len(it['shields']) - 1}\n#define ENEMY_NUM {len(it['enemies']) - 1}\n"
         "extern const Weapon weapons[WEAP_NUM + 1];\n"
         "extern const WeaponPort weapon_ports[PORT_NUM + 1];\n"
         "extern const Ship ships[SHIP_NUM + 1];\n"
         "extern const uint8_t generator_power[POWER_NUM + 1];   /* powerAdd per tick */\n"
         "extern const Shield shields[SHIELD_NUM + 1];\n"
         "#define SPECIAL_NUM 46\nextern const Special specials[SPECIAL_NUM + 1];\n"
         "extern const char *const special_names[SPECIAL_NUM + 1];\n"
         "extern const Option options[OPTION_NUM + 1];\n"
         "extern const char *const option_names[OPTION_NUM + 1];\n"
         "extern const uint16_t option_cost[OPTION_NUM + 1];\n"
         "extern const EnemyDat enemy_dat[ENEMY_NUM + 1];\n"
         "extern const int16_t enemy_value_scale[12 * (ENEMY_NUM + 1)];\n"
         "/* shop data: names and costs */\n"
         "extern const char *const port_names[PORT_NUM + 1], *const power_names[POWER_NUM + 1];\n"
         "extern const char *const shield_names[SHIELD_NUM + 1], *const ship_names[SHIP_NUM + 1];\n"
         "extern const uint16_t port_cost[PORT_NUM + 1], power_cost[POWER_NUM + 1];\n"
         "extern const uint16_t shield_cost[SHIELD_NUM + 1], ship_cost[SHIP_NUM + 1];\n"
         "#endif\n")

    def arr(v):
        return "{" + ",".join(str(x) for x in v) + "}"
    rows = []
    for w in it["weapons"]:
        rows.append("{%d,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%d,%d,%d,%d,%d,%d}" % (
            w["shotrepeat"], w["multi"], w["weapani"], w["max"], w["tx"], w["ty"], w["aim"], arr(w["attack"]), arr(w["del_"]),
            arr(w["sx"]), arr(w["sy"]), arr(w["bx"]), arr(w["by"]), arr(w["sg"]), w["acceleration"],
            w["accelerationx"], w["circlesize"], w["sound"], w["trail"], w["shipblastfilter"]))
    c = emit.HEADER + '#include "items.h"\n'
    c += "const Weapon weapons[WEAP_NUM + 1] = {\n    " + ",\n    ".join(rows) + "\n};\n"
    c += "const WeaponPort weapon_ports[PORT_NUM + 1] = {\n    " + ",\n    ".join(
        "{{%s,%s},%d,%d}" % (arr(p["op"][0]), arr(p["op"][1]), p["poweruse"], p["opnum"]) for p in it["ports"]) + "\n};\n"
    c += "const Ship ships[SHIP_NUM + 1] = {\n    " + ",\n    ".join(
        "{%d,%d,%d,%d}" % (s["shipgraphic"], s["spd"], s["dmg"], s["ani"]) for s in it["ships"]) + "\n};\n"
    c += "const Shield shields[SHIELD_NUM + 1] = {" + ",".join(
        "{%d,%d}" % (x["tpwr"], x["mpwr"]) for x in it["shields"]) + "};\n"
    c += "const Special specials[SPECIAL_NUM + 1] = {" + ",".join(
        "{%d,%d,%d}" % (s["wpn"], s["pwr"], s["stype"]) for s in it["specials"]) + "};\n"
    c += "const char *const special_names[] = {" + ",".join(
        '"' + s["name"].strip() + '"' for s in it["specials"]) + "};\n"
    c += "const Option options[OPTION_NUM + 1] = {\n    " + ",\n    ".join(
        "{%s,%d,%d,%d,%d,%d,%d,%d}" % (arr(o["gr"]), o["wpnum"], o["pwr"], o["tr"],
                                        o["option"], o["ani"], o["ammo"], o["wport"])
        for o in it["options"]) + "\n};\n"
    c += "const EnemyDat enemy_dat[ENEMY_NUM + 1] = {\n    " + ",\n    ".join(
        "{%d,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}" % (
            e["ani"], arr(e["tur"]), arr(e["freq"]), e["xmove"], e["ymove"], e["xaccel"], e["yaccel"],
            e["xcaccel"], e["ycaccel"], e["startx"], e["starty"], e["startxc"], e["startyc"], e["armor"],
            e["esize"], arr(e["egraphic"]), e["explosiontype"], e["animate"], e["shapebank"], e["xrev"],
            e["yrev"], e["dgr"], e["dlevel"], e["dani"], e["elaunchfreq"], e["elaunchtype"], e["value"],
            e["eenemydie"]) for e in it["enemies"]) + "\n};\n"
    c += emit.c_array("int16_t", "enemy_value_scale", [
        difficulty_mod.enemy_value(e["value"], d) for d in range(-1, 11) for e in it["enemies"]], 16, "{}")
    c += "const uint8_t generator_power[POWER_NUM + 1] = " + arr(p["power"] for p in it["powers"]) + ";\n"
    def names(tab):
        return "{" + ",".join('"' + t["name"].strip().replace('"', "'") + '"' for t in tab) + "}"
    for nm, key in (("port", "ports"), ("power", "powers"), ("shield", "shields"), ("ship", "ships"), ("option", "options")):
        c += f"const char *const {nm}_names[] = {names(it[key])};\n"
        c += f"const uint16_t {nm}_cost[] = " + arr(t["cost"] for t in it[key]) + ";\n"
    emit.write_if_changed(os.path.join(out, "items.h"), h)
    emit.write_if_changed(os.path.join(out, "items.c"), c)
    return it


SHIP_BANK = range(-4, 5, 2)     # ship_sprite offsets for banking -2..2


SHAPE_FILE = "2478ABCDEFGHIJKLMNOPQRSTU5#V0@3^59"   # lvlmast.c:shapeFile, bank n -> [n-1]
BANK_COINS, BANK_TWO_PLAYER = 21, 26              # tyrian.shp tables 10 / 9 (JE_makeEnemy)


def weapon_frames(it, wids):
    """Sprite2 frames (1..1000, >500 = sheet 12) the given weapons can show:
    sg + animation steps, mod 1000 (shots.c / JE_main conventions)."""
    used = set()
    for wid in wids:
        if not wid or wid > len(it["weapons"]) - 1:
            continue
        w = it["weapons"][wid]
        for k in range(max(1, min(8, w["max"]))):
            g, d = w["sg"][k], w["del_"][k]
            if not g or g > 60000:
                continue
            for a in range(max((d - 100 + 1) if 100 < d < 120 else w["weapani"] + 1, w["weapani"] + 1)):
                f = g + a
                used.add(f % 1000 if f > 1000 else f)
    return used


def tiles_column_major(px, wc, hc):
    """(hc*8, wc*8) indices -> 4bpp longs in VDP sprite order (columns)."""
    out = []
    for cx in range(wc):
        for cy in range(hc):
            for row in px[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8]:
                v = 0
                for p in row:
                    v = (v << 4) | int(p)
                out.append(v)
    return out


def _piece(sheet, idx):
    """Sprite2 piece -> 16x16 int array (12x14 used), -1 transparent."""
    img = np.full((16, 16), -1, np.int32)
    img[0:14, 0:12] = [[-1 if p is None else p for p in row] for row in shapes.decode_sprite2(sheet, idx)]
    return img


def _hist(imgs):
    hist = {}
    for img in imgs:
        v, n = np.unique(img[img >= 0], return_counts=True)
        for a, m in zip(v, n):
            hist[int(a)] = hist.get(int(a), 0) + int(m)
    return hist


def gen_sprites(out, it, lvls, bg_pals):
    """Sprite graphics.  Palette line 2: player ships and all shots (player
    and enemy shots share sheets 8/12).  Palette line 3: the enemy banks the
    levels load (event 5), coins/items (banks 21, 26) and explosions
    (newsh6).  Pieces are 12x14 Sprite2 frames in 2x2-cell sprites.
    lvls / bg_pals: the converted levels and their background palettes."""
    tabs = shapes.main_shape_tables()
    rgb = palette.load_palettes()[GAME_PALETTE]

    ship_imgs = []
    for sh in it["ships"][1:]:
        for b in SHIP_BANK:
            g = sh["shipgraphic"] + b
            img = np.full((32, 24), -1, np.int32)
            for (dx, dy), off in (((0, 0), 0), ((12, 0), 1), ((0, 14), 19), ((12, 14), 20)):
                img[dy:dy + 16, dx:dx + 12] = _piece(tabs[8], g + off)[:, :12]
            ship_imgs.append(img[:32])

    wids = {w for p in it["ports"][1:] for m in p["op"] for w in m}
    wids |= {s["wpn"] for s in it["specials"] if s["stype"] in (1, 5, 6, 7, 8, 9, 10, 11, 16)}
    wids |= {104, 535}  # orbiting asteroid killer pickup / superbomb
    wids |= {o["wpnum"] + charge for o in it["options"][1:] for charge in range(o["pwr"] + 1)}
    def level_banks(lvl):
        return {d for e in lvl.events if e.type == 5 for d in (e.dat, e.dat2, e.dat3, e.dat4) if 0 < d <= 34}
    banks = sorted(set().union(*(level_banks(lv) for lv in lvls)))
    enemy_ids = set(range(len(it["enemies"])))   # any enemy can be spawned by events / deaths
    wids |= {t for i in enemy_ids for t in it["enemies"][i]["tur"] if 0 < t < 252}
    wids = items.weapon_dependencies(it, wids)
    frames = sorted(weapon_frames(it, wids))

    gfx, pal_of = [], []                         # pieces in id order, palette set
    shot_index = [0xFFFF] * 1001
    for f in frames:
        shot_index[f] = len(gfx)
        gfx.append(_piece(tabs[11], f - 500) if f > 500 else _piece(tabs[7], f))
        pal_of.append(0)

    star_gfx = []
    for color in range(0x90, 0xa0):
        star_gfx.append(len(gfx))
        img = np.full((16, 16), -1, np.int32)
        img[1, 1] = color
        if color >= 0x94:
            for x, y in ((0, 1), (2, 1), (1, 0), (1, 2)):
                img[y, x] = color - 4
        gfx.append(img)
        pal_of.append(0)

    # Sidekick artwork shares the player's palette. Styles 1/2 use the
    # large sprite sheet; the rest use individual ship-sheet pieces.
    option_small = [0xFFFF] * 305
    option_big = [0xFFFF] * 305
    option_big_imgs = {}
    for o in it["options"][1:]:
        for frame in o["gr"][:o["ani"]]:
            for charge in range(o["pwr"] + 1):
                f = frame + charge
                if not 0 < f <= 304:
                    continue
                if o["tr"] in (1, 2):
                    img = np.full((32, 24), -1, np.int32)
                    for (dx, dy), off in (((0, 0), 0), ((12, 0), 1), ((0, 14), 19), ((12, 14), 20)):
                        if f + off <= 304:
                            img[dy:dy + 14, dx:dx + 12] = _piece(tabs[9], f + off)[:14, :12]
                    option_big_imgs[f] = img
                elif option_small[f] == 0xFFFF:
                    option_small[f] = len(gfx)
                    gfx.append(_piece(tabs[8], f))
                    pal_of.append(0)

    # OPTION_SHAPES projectiles (e.g. Plasma Storm): retain original bounds,
    # split artwork into cached 24x32 hardware sprites, all on player PAL2.
    special_sprites = shapes.decode_sprite_table(tabs[5])
    special_imgs = {}
    for wid in sorted(wids):
        if not 0 < wid < len(it["weapons"]):
            continue
        w = it["weapons"][wid]
        for k in range(max(1, min(8, w["max"]))):
            gr, delay = w["sg"][k], w["del_"][k]
            count = max((delay - 100 + 1) if 100 < delay < 120 else w["weapani"] + 1, w["weapani"] + 1)
            for ani in range(count):
                f = gr + ani - 60001
                if not 0 <= f < len(special_sprites) or special_sprites[f] is None or f in special_imgs:
                    continue
                full = np.array([[-1 if p is None else p for p in row] for row in special_sprites[f]], np.int32)
                parts = []
                for y in range(0, len(full), 32):
                    for x in range(0, full.shape[1], 24):
                        img = np.full((32, 24), -1, np.int32)
                        sub = full[y:y + 32, x:x + 24]
                        img[:sub.shape[0], :sub.shape[1]] = sub
                        if (img >= 0).any():
                            parts.append((x, y, img))
                special_imgs[f] = parts

    # 12x14 pieces actually drawn: frames of 1x1 enemies/items (2x2 ones use
    # the composed big frames below), custom enemies of events 49-52, and
    # the explosion sprites (varz.c explosion_data: sprite+1 .. +ttl)
    needed = {}
    for d in it["enemies"]:
        if d["esize"] == 1:
            continue
        fr = needed.setdefault(d["shapebank"], set())
        fr.update(g for g in d["egraphic"] if 0 < g <= 304)
        fr.update(d["dgr"] + k for k in range(max(1, abs(d["dani"]))) if 0 < d["dgr"] + k <= 304)
    for lv in lvls:
        for e in lv.events:
            if 49 <= e.type <= 52 and e.dat3 > 0:
                needed.setdefault(e.dat3, set()).add(e.dat)
    explo_sprites = (144, 120, 190, 209, 152, 171, 133, 1, 20, 39, 58, 110, 76, 91, 227, 230, 233, 252, 246,
                     249, 265, 268, 271, 236, 239, 242, 261, 274, 277, 280, 299, 284, 287, 290, 293, 165, 184,
                     203, 222, 168, 187, 206, 225, 169, 188, 207, 226, 170, 189, 208, 246, 227, 265)
    explo_ttl = (7, 12, 12, 12, 12, 12, 7, 12, 12, 12, 12, 3, 7, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                 3, 3, 3, 3, 3, 3, 3, 8, 8, 8, 8, 8, 8, 8, 10, 10, 10, 20, 14, 14, 14, 14, 14, 14, 14)
    needed["explo"] = {sp + 1 + k for sp, t in zip(explo_sprites, explo_ttl) for k in range(t + 1)}

    def add_sheet(sheet, key):
        index = [0xFFFF] * 305
        want = needed.get(key, set())
        for i in range(1, min(304, shapes.sprite2_count(sheet)) + 1):
            if i not in want:
                continue
            img = _piece(sheet, i)
            if (img >= 0).any():
                index[i] = len(gfx)
                gfx.append(img)
                pal_of.append(1)
        return index
    bank_index = {}
    for bk in banks:
        name = f"newsh{SHAPE_FILE[bk - 1].lower()}.shp"
        if os.path.exists(data_path(name)):   # q, @ and & are named but missing
            bank_index[bk] = add_sheet(shapes.load_sheet(name), bk)
    bank_index[BANK_COINS] = add_sheet(tabs[10], BANK_COINS)
    bank_index[BANK_TWO_PLAYER] = add_sheet(tabs[9], BANK_TWO_PLAYER)
    explo_index = add_sheet(shapes.load_sheet("newsh6.shp"), "explo")

    # 2x2 enemies (esize 1): one 3x4-cell sprite per frame instead of four
    # pieces.  Frames: egraphic[0..ani-1], dgr and the damaged animation.
    sheets = {bk: (tabs[10] if bk == BANK_COINS else tabs[9] if bk == BANK_TWO_PLAYER else
                   shapes.load_sheet(f"newsh{SHAPE_FILE[bk - 1].lower()}.shp")) for bk in bank_index}
    big_frames = {bk: set() for bk in bank_index}
    for e in it["enemies"]:
        if e["esize"] != 1 or e["shapebank"] not in big_frames:
            continue
        fr = big_frames[e["shapebank"]]
        # all 20 entries: events 20/24 can animate past enemyDat.ani
        fr.update(g for g in e["egraphic"] if 0 < g < 285)
        if 0 < e["dgr"] < 285:
            fr.add(e["dgr"])
            for k in range(abs(e["dani"])):
                if e["dgr"] + k < 285:
                    fr.add(e["dgr"] + k)
    big_imgs, big_pal, big_index = [], [], {}
    pieces = {}                    # (bank, frame) -> composed image of a map piece
    for bk, fr in sorted(big_frames.items()):
        idx = [0xFFFF] * 285
        for f in sorted(fr):
            img = np.full((32, 24), -1, np.int32)
            for (dx, dy), off in (((0, 0), 0), ((12, 0), 1), ((0, 14), 19), ((12, 14), 20)):
                if f + off <= 304:
                    p = _piece(sheets[bk], f + off)[:14, :12]
                    blk = img[dy:dy + 14, dx:dx + 12]
                    img[dy:dy + 14, dx:dx + 12] = np.where(p >= 0, p, blk)
            if not (img >= 0).any():
                continue
            # frames that cover (almost) the whole 24x28 are pieces of map
            # (destructible ground): they must match the level's map palette,
            # so they get one copy per level (below)
            if (img[:28] >= 0).mean() > 0.85:
                pieces[(bk, f)] = img
            else:
                idx[f] = len(big_imgs)
                big_imgs.append(img)
                big_pal.append(3)
        big_index[bk] = idx

    pals = []
    for k in (0, 1):
        imgs = [g for g, p in zip(gfx, pal_of) if p == k] + (
            ship_imgs + list(option_big_imgs.values()) + [img for parts in special_imgs.values() for _, _, img in parts] if k == 0 else big_imgs)
        pal = (np.array([palette.cram_to_rgb(word) for word in PLAYER_CRAM], np.float64) if k == 0 else
               quantize.quantize_cells([_hist(imgs)], rgb, n_pals=1, k=15)[0][0])
        if k == 0:   # line 2 indexes 14/15: opaque black and white for text (HUD, menus)
            pal = np.vstack([pal, np.zeros((13 - len(pal), 3)), [[0, 0, 0]], [[255, 255, 255]]])
        pals.append(pal)
    luts = [quantize.remap(np.arange(256), rgb, p) for p in pals]

    def q(img, k):
        return np.where(img >= 0, luts[k][np.clip(img, 0, 255)], 0)
    ship_tiles = [t for img in ship_imgs for t in tiles_column_major(q(img, 0), 3, 4)]
    spr_tiles = [t for img, k in zip(gfx, pal_of) for t in tiles_column_major(q(img, k), 2, 2)]
    big_tiles = [t for img in big_imgs for t in tiles_column_major(q(img, 1), 3, 4)]
    # map pieces: per level, quantized with that level's background palettes
    # (lines 0/1); each level gets its own bank -> frame tables, limited to
    # the frames of enemies the level can spawn
    lifted = quantize.lift(rgb)

    def level_frames(lv):
        spawn = {6: 0, 7: 0, 10: 0, 15: 0, 17: 0, 18: 0, 23: 0, 32: 0, 56: 0}
        ids = set(lv.enemies)
        for e in lv.events:
            if e.type in spawn:
                ids.add(e.dat)
            elif e.type == 12:
                ids.update(e.dat + k for k in range(4))
            elif e.type in (33, 45):
                ids.update([e.dat] + list(range(829, 835)))
        todo = list(ids)
        while todo:
            i = todo.pop()
            if not 0 <= i < len(it["enemies"]):
                continue
            d = it["enemies"][i]
            for n in (d["eenemydie"], d["elaunchtype"] % 1000):
                if n and n not in ids:
                    ids.add(n)
                    todo.append(n)
        fr = set()
        for i in ids:
            if 0 <= i < len(it["enemies"]):
                d = it["enemies"][i]
                fr.update((d["shapebank"], g) for g in d["egraphic"])
                fr.update((d["shapebank"], d["dgr"] + k) for k in range(max(1, abs(d["dani"]))))
        return fr
    level_tables = []              # per level: {bank: idx list}
    for lv, bpals in zip(lvls, bg_pals):
        bg_luts = [quantize.remap(np.arange(256), rgb, p) for p in bpals]
        tabs_lv = {}
        used = level_frames(lv)
        for bk in sorted(level_banks(lv) & set(big_index)):
            idx = None
            for (pb, f), img in sorted(pieces.items()):
                if pb != bk or (pb, f) not in used:
                    continue
                if idx is None:
                    idx = list(big_index[bk])
                px = img[img >= 0]
                g = int(np.argmin([quantize._dist(lifted[px], p).min(1).sum() for p in bpals]))
                idx[f] = len(big_imgs)
                big_imgs.append(img)
                big_pal.append(g)
                big_tiles += tiles_column_major(np.where(img >= 0, bg_luts[g][np.clip(img, 0, 255)], 0), 3, 4)
            if idx is not None:
                tabs_lv[bk] = idx
        level_tables.append(tabs_lv)
    # Append large companions after enemy/map frames; PAL2 preserves their
    # player colours without using an extra hardware palette.
    for f, img in sorted(option_big_imgs.items()):
        option_big[f] = len(big_imgs)
        big_imgs.append(img)
        big_pal.append(2)
        big_tiles += tiles_column_major(q(img, 0), 3, 4)
    special_starts = [0] * len(special_sprites)
    special_counts = [0] * len(special_sprites)
    special_parts = []
    for f, parts in sorted(special_imgs.items()):
        special_starts[f] = len(special_parts)
        special_counts[f] = len(parts)
        for x, y, img in parts:
            special_parts.append((len(big_imgs), x, y))
            big_imgs.append(img)
            big_pal.append(2)
            big_tiles += tiles_column_major(q(img, 0), 3, 4)
    words = []
    for p in pals:
        w = [0] + [palette.to_cram(tuple(int(x) for x in c)) for c in p]
        words += w + [0] * (16 - len(w))

    beam_index = len(big_imgs)
    big_imgs.append(None)
    big_tiles.extend([0xffffffff] * 96)
    big_pal.append(2)
    h = (emit.HEADER + "#ifndef GEN_SPRITES_H\n#define GEN_SPRITES_H\n#include <types.h>\n"
         "/* palette lines 2 (player, shots) and 3 (enemies, items, explosions) */\n"
         "extern const u16 spr_palettes[32];\n"
         f"#define SHIP_BANK_FRAMES {len(SHIP_BANK)}\n"
         "/* ship s (1-based), banking b (-2..2): 12 tiles (3x4 cells) at\n"
         " * ship_tiles + ((s-1) * SHIP_BANK_FRAMES + b + 2) * 96 */\n"
         "extern const u32 ship_tiles[];\n"
         f"#define SPR_GFX_COUNT {len(gfx)}\n"
         "/* gfx i: 4 tiles (2x2 cells) at spr_tiles + i * 32, palette line spr_gfx_pal[i] */\n"
         "extern const u32 spr_tiles[];\n"
         "extern const u8 spr_gfx_pal[SPR_GFX_COUNT];\n"
         "#define GFX_NONE 0xFFFF\n"
         "/* shot frame (1..1000, >500 = sheet 12) -> gfx */\n"
         "extern const u16 shot_gfx[1001];\n"
         "extern const u16 beam_gfx[1], star_gfx[16];\n"
         "extern const u16 option_small_gfx[305], option_big_gfx[305];\n"
         "typedef struct { u16 gfx; u8 x, y; } SpecialPiece;\n"
         "extern const SpecialPiece special_pieces[];\n"
         f"extern const u16 special_starts[{len(special_sprites)}];\n"
         f"extern const u8 special_counts[{len(special_sprites)}];\n"
         "/* enemy shape bank (1..34, 21 coins, 26 two-player) -> 305 entries\n"
         " * (sprite 1..304 -> gfx), NULL when the bank is not converted */\n"
         "extern const u16 *const bank_gfx[35];\n"
         "extern const u16 explo_gfx[305];   /* newsh6.shp */\n"
         f"#define BIG_GFX_COUNT {len(big_imgs)}\n"
         "/* 2x2 enemies: bank -> 285 entries (frame -> big gfx or GFX_NONE),\n"
         " * big gfx i: 12 tiles (3x4 cells) at big_tiles + i * 96, palette line\n"
         " * big_gfx_pal[i] (3, or 0/1 = the map's for map pieces).\n"
         " * big_gfx_level[level][bank]: the level's table (map pieces differ) */\n"
         "extern const u16 *const *const big_gfx_level[];\n"
         "extern const u8 big_gfx_pal[];\n"
         "extern const u32 big_tiles[];\n#endif\n")
    c = emit.HEADER + '#include "sprites.h"\n'
    c += emit.c_array("u16", "spr_palettes", words)
    c += emit.tile_array("ship_tiles", ship_tiles)
    c += emit.tile_array("spr_tiles", spr_tiles)
    c += emit.c_array("u8", "spr_gfx_pal", [2 + k for k in pal_of], 32, "{}")
    c += emit.c_array("u16", "shot_gfx", shot_index, 16, "{}")
    c += emit.c_array("u16", "option_small_gfx", option_small)
    c += emit.c_array("u16", "option_big_gfx", option_big)
    c += emit.c_array("u16", "star_gfx", star_gfx)
    c += emit.c_array("u16", "beam_gfx", [beam_index])
    c += emit.c_array("u16", "special_starts", special_starts)
    c += emit.c_array("u8", "special_counts", special_counts, 16, "{}")
    c += "const SpecialPiece special_pieces[] = {" + ",".join(
        "{%d,%d,%d}" % part for part in special_parts or [(65535, 0, 0)]) + "};\n"
    for bk, idx in sorted(bank_index.items()):
        c += emit.c_array("u16", f"bank{bk}_gfx", idx, 16, "{}")
    c += "const u16 *const bank_gfx[35] = {" + ",".join(
        f"bank{i}_gfx" if i in bank_index else "0" for i in range(35)) + "};\n"
    c += emit.c_array("u16", "explo_gfx", explo_index, 16, "{}")
    for bk, idx in sorted(big_index.items()):
        c += emit.c_array("u16", f"big{bk}_gfx", idx, 16, "{}")
    for li, tl in enumerate(level_tables):
        for bk, idx in sorted(tl.items()):
            c += emit.c_array("u16", f"big{bk}_lv{li}_gfx", idx, 16, "{}")
        c += f"static const u16 *const big_lv{li}[35] = {{" + ",".join(
            f"big{i}_lv{li}_gfx" if i in tl else (f"big{i}_gfx" if i in big_index else "0")
            for i in range(35)) + "};\n"
    c += "const u16 *const *const big_gfx_level[] = {" + ",".join(
        f"big_lv{li}" for li in range(len(level_tables))) + "};\n"
    c += emit.tile_array("big_tiles", big_tiles or [0])
    c += emit.c_array("u8", "big_gfx_pal", big_pal or [3], 32, "{}")
    emit.write_if_changed(os.path.join(out, "sprites.h"), h)
    emit.write_if_changed(os.path.join(out, "sprites.c"), c)
    print(f"   gen: sprites: {len(ship_imgs)} ship frames, {len(gfx)} pieces "
          f"({len(frames)} shot frames, banks {sorted(bank_index)}), {len(big_imgs)} big enemy frames "
          f"({sum(1 for p in big_pal if p < 2)} map pieces over the levels), "
          f"colors {[len(p) for p in pals]}")


def gen_difficulty(out):
    h = (emit.HEADER + '#ifndef GEN_DIFFICULTY_H\n#define GEN_DIFFICULTY_H\n#include "game/types.h"\n'
         'extern const uint8_t enemy_armor_scale[12 * 256];\n'
         'extern const uint32_t score_thresholds[9 * 8];\n#endif\n')
    c = emit.HEADER + '#include "difficulty.h"\n'
    c += emit.c_array("uint8_t", "enemy_armor_scale", [difficulty_mod.enemy_armor(a, d)
                                                               for d in range(-1, 11) for a in range(256)], 32, "{}")
    c += emit.c_array("uint32_t", "score_thresholds", [v for d in range(1, 10)
                                                               for v in difficulty_mod.score_thresholds(d)], 8, "{}")
    emit.write_if_changed(os.path.join(out, "difficulty.h"), h)
    emit.write_if_changed(os.path.join(out, "difficulty.c"), c)


def gen_shot_bounds(out):
    sprites = shapes.decode_sprite_table(shapes.main_shape_tables()[5])
    h = (emit.HEADER + '#ifndef GEN_SHOT_SHAPES_H\n#define GEN_SHOT_SHAPES_H\n#include "game/types.h"\n'
         + f'#define OPTION_SHAPE_COUNT {len(sprites)}\n'
         + 'extern const uint8_t option_shape_width[OPTION_SHAPE_COUNT];\n'
         + 'extern const uint8_t option_shape_height[OPTION_SHAPE_COUNT];\n#endif\n')
    c = emit.HEADER + '#include "shot_shapes.h"\n'
    c += emit.c_array("uint8_t", "option_shape_width", [len(s[0]) if s else 0 for s in sprites], 16, "{}")
    c += emit.c_array("uint8_t", "option_shape_height", [len(s) if s else 0 for s in sprites], 16, "{}")
    emit.write_if_changed(os.path.join(out, "shot_shapes.h"), h)
    emit.write_if_changed(os.path.join(out, "shot_shapes.c"), c)


def gen_motion(out):
    # sin/cos are build-time only. A 32-bit phase avoids orbit drift; the
    # 1024-entry, radius-20 LUT rounds to the PC's pixel coordinates.
    values = [math.floor(20 * math.sin(2 * math.pi * i / 1024) + 0.5) for i in range(1024)]
    emit.write_if_changed(os.path.join(out, "motion.h"), emit.HEADER +
                          '#ifndef GEN_MOTION_H\n#define GEN_MOTION_H\n#include "game/types.h"\n'
                          'extern const int8_t orbit_sine[1024];\n#endif\n')
    emit.write_if_changed(os.path.join(out, "motion.c"), emit.HEADER + '#include "motion.h"\n' +
                          emit.c_array("int8_t", "orbit_sine", values, 32, "{}"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out_dir", nargs="?", default=os.path.join("src", "gen_data"))
    parser.add_argument("--episode", type=int, choices=range(1, 5), default=1)
    args = parser.parse_args()
    out, episode = args.out_dir, args.episode
    from tyrdata.combine import generate_episodes
    generate_episodes(out, episode, gen_level_bg, write_level_bgs, gen_levels,
                      gen_script, gen_items, gen_sprites)
    gen_palettes(out)
    gen_filters(out)
    gen_difficulty(out)
    gen_shot_bounds(out)
    gen_motion(out)
    gen_fonts(out)
    gen_title(out)
    from tyrdata.story import generate as gen_story
    gen_story(out, tiles_column_major)
    from tyrdata.cubes import generate as gen_cubes
    gen_cubes(out, tiles_column_major)
    res = gen_sounds(out, "res")
    # music: VGMs from tools/music_vgm.py (make music) copied to res/music
    songs = sorted(f for f in os.listdir(os.path.join("res", "music")) if f.endswith(".vgm")) \
        if os.path.isdir(os.path.join("res", "music")) else []
    if songs:
        res += "XGM2 music " + " ".join(f'"music/{f}"' for f in songs) + "\n"
    emit.write_if_changed(os.path.join("res", "resources.res"), res)
    emit.write_if_changed(os.path.join(out, "music.h"), emit.HEADER +
                          f"#ifndef GEN_MUSIC_H\n#define GEN_MUSIC_H\n#define MUSIC_TRACKS {len(songs)}\n#endif\n")
    print(f"   gen: {out}")


if __name__ == "__main__":
    main()
