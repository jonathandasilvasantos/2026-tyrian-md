"""Original story pictures and proportional TINY_FONT pages, tiled at build time."""
import os

import numpy as np

from . import data_path, emit, palette, quantize, raster, script, shapes

FONT_MAP = ([-1] * 33 + [26, 33, 60, 61, 62, -1, 32, 64, 65, 63, 84, 29, 83, 28, 80,
            79, 70, 71, 72, 73, 74, 75, 76, 77, 78, 31, 30, -1, 85, -1, 27, -1]
            + list(range(26)) + [68, 82, 69, -1, -1, -1] + list(range(34, 60))
            + [66, 81, 67, -1, -1])


def text_width(text, glyphs):
    width = 0
    for char in text:
        index = FONT_MAP[ord(char)] if ord(char) < len(FONT_MAP) else -1
        if char == ' ':
            width += 6
        elif index >= 0 and glyphs[index]:
            width += len(glyphs[index][0]) + 1
    return width


def draw_text(image, text, x, y, glyphs):
    """Original proportional advances and one-pixel shadow; no line wrapping."""
    for char in text:
        index = FONT_MAP[ord(char)] if ord(char) < len(FONT_MAP) else -1
        if char == ' ':
            x += 6
        elif index >= 0 and glyphs[index]:
            glyph = glyphs[index]
            for dy, row in enumerate(glyph):
                for dx, pixel in enumerate(row):
                    if pixel is not None and pixel > 4 and x + dx + 1 < 320 and y + dy + 1 < 224:
                        image[y + dy + 1, x + dx + 1] = 14
            for dy, row in enumerate(glyph):
                for dx, pixel in enumerate(row):
                    if pixel is not None and pixel > 4 and x + dx < 320 and y + dy < 224:
                        image[y + dy, x + dx] = 15
            x += len(glyph[0]) + 1


def page_bitmap(page, glyphs, prompt):
    image = np.zeros((224, 320), np.uint8)
    y = (2 if page['red'] else 55) + 12
    for line in page['lines']:
        draw_text(image, line, 10, y, glyphs)
        y += 10
    draw_text(image, prompt, (320 - text_width(prompt, glyphs)) // 2,
              (118 if page['red'] else 184) + 12, glyphs)
    return image


def tiled(image, pack, assignments=None, luts=None):
    ids, words, tilemap = {}, [], []
    for i, cell in enumerate(raster.cells(image)):
        group = int(assignments[i]) if assignments is not None else 0
        pixels = luts[group][cell].astype(np.uint8) if luts is not None else cell
        key = pixels.tobytes()
        if key not in ids:
            ids[key] = len(ids)
            words += pack(pixels, 1, 1)
        line = (0, 1, 3)[group] if assignments is not None else 2
        tilemap.append(ids[key] | (line << 13))
    return words, tilemap


def generate(out, pack):
    from PIL import Image
    glyphs = shapes.decode_sprite_table(shapes.main_shape_tables()[2])
    # helptext.c: miscText[4], not a replacement prompt invented for the port.
    prompt = script.help_section('** Section 4')[4]
    source = emit.HEADER + '#include "story.h"\n'
    descriptors = []
    page_count, max_text = 0, 0
    for episode in range(1, 5):
        pages = script.compile_script(episode)[4]
        rows = []
        for index, page in enumerate(pages):
            prefix = f'story_{episode}_{index}'
            words, tilemap = tiled(page_bitmap(page, glyphs, prompt), pack)
            count = len(words) // 8
            max_text = max(max_text, count)
            source += emit.tile_array(prefix + '_tiles', words)
            source += emit.c_array('u16', prefix + '_map', tilemap, 40)
            rows.append('{' + f'{prefix}_tiles,{prefix}_map,{count},{int(page["warning"])}' + '}')
        descriptors.append('{' + ','.join(rows) + '}')
        page_count += len(pages)
    source += 'const StoryPage story_pages[4][32] = {' + ','.join(descriptors) + '};\n'
    picture_rows = []
    max_picture = 0
    for number in (0, 5, 7, 8, 9, 13):
        if number == 0:
            picture = Image.open(data_path('tshp2.pcx'))
            image = np.array(picture, np.uint8)
            rgb = np.array(picture.getpalette()[:768], np.float64).reshape(256, 3)
        else:
            pixels, pal = shapes.load_pic(number)
            image = np.frombuffer(pixels, np.uint8).reshape(200, 320)
            rgb = np.array(palette.load_palettes()[pal], np.float64)
        image = np.pad(image, ((12, 12), (0, 0)))
        histograms = []
        for cell in raster.cells(image):
            colors, counts = np.unique(cell, return_counts=True)
            histograms.append(dict(zip(map(int, colors), map(int, counts))))
        pals, assignments = quantize.quantize_cells(histograms, rgb, n_pals=3)
        luts = [quantize.remap(np.arange(256), rgb, p) for p in pals]
        words, tilemap = tiled(image, pack, assignments, luts)
        count = len(words) // 8
        max_picture = max(max_picture, count)
        prefix = f'story_pic_{number}'
        source += emit.tile_array(prefix + '_tiles', words)
        source += emit.c_array('u16', prefix + '_map', tilemap, 40)
        cram = [0] * 64
        for pal, line in zip(pals, (0, 1, 3)):
            cram[line * 16:line * 16 + 16] = [0] + [palette.to_cram(c) for c in pal]
        source += emit.c_array('u16', prefix + '_palette', cram)
        picture_rows.append('{' + f'{prefix}_tiles,{prefix}_map,{prefix}_palette,{count},{number}' + '}')
    # Text screens reuse both font regions: hud_init reloads the HUD font
    # on level entry. Shops retain their 192 menu-font tiles.
    assert 16 + max_picture + max_text + 1 <= 1536, (max_picture, max_text)
    assert 208 + max_picture <= 1440
    source += 'const StoryPicture story_pictures[6] = {' + ','.join(picture_rows) + '};\n'
    header = (emit.HEADER + '#ifndef GEN_STORY_H\n#define GEN_STORY_H\n#include <types.h>\n'
              'typedef struct { const u32 *tiles; const u16 *map; u16 count; u8 warning; } StoryPage;\n'
              'typedef struct { const u32 *tiles; const u16 *map, *palette; u16 count; u8 number; } StoryPicture;\n'
              'extern const StoryPage story_pages[4][32];\n'
              'extern const StoryPicture story_pictures[6];\n#endif\n')
    emit.write_if_changed(os.path.join(out, 'story.h'), header)
    emit.write_if_changed(os.path.join(out, 'story.c'), source)
    print(f'   gen: story: {page_count} pages, 6 pictures; max {max_picture}+{max_text} tiles')
