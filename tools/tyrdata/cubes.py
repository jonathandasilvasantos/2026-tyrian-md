"""Datacube reader and PC word wrapping (game_menu.c:load_cube).

Text is rasterized once at build time into shared 8x8 tiles. The Genesis
streams just nine lines into alternate VRAM pages, without a framebuffer.
"""
import os

import numpy as np

from . import data_path, emit, palette, quantize, shapes
from .binfile import read_encrypted_strings
from .story import FONT_MAP, text_width
from .script import atoi


def load(episode):
    source = read_encrypted_strings(open(data_path(f'cubetxt{episode}.dat'), 'rb').read())
    records = []
    for line in source:
        if line.startswith('*'):
            records.append(dict(marker=line, text=[]))
        elif records:
            records[-1]['text'].append(line)
    cubes = []
    for record in records:
        text = record['text']
        cubes.append(dict(face=atoi(record['marker'][4:]) - 1,
                          title=(text[0] if text else '')[:80],
                          header=(text[1] if len(text) > 1 else '')[:12],
                          body=text[2:]))
    return cubes


def wrap(body, glyphs):
    """Keep the PC's 35-char/150-pixel bounds, leading space and paragraph gaps."""
    lines = [''] * 90
    line = chars = width = last = 0
    for source in body:
        if not source:
            line += 4 if chars == 0 else 1
            chars = width = 0
            continue
        for word in source.split(' '):
            count, pixels = len(word), text_width(word, glyphs)
            if count > 35 or pixels > 150:
                break
            prepend = True
            chars += count + 1
            width += pixels + 6
            if chars > 35 or width > 150:
                line += 1
                chars, width, prepend = count, pixels, False
            if line < 90:
                lines[line] += (' ' if prepend else '') + word
                last = line + 1
    return lines[:last]


def line_bitmap(text, glyphs):
    image = np.zeros((8, 152), np.uint8)
    x, bright = 1, False
    for char in text:
        if char == '~':
            bright = not bright
            continue
        index = FONT_MAP[ord(char)] if ord(char) < len(FONT_MAP) else -1
        if char == ' ':
            x += 6
        elif index >= 0 and glyphs[index]:
            glyph = glyphs[index]
            for y, row in enumerate(glyph):
                for dx, pixel in enumerate(row):
                    if pixel is not None and pixel > 4 and x + dx + 1 < 152 and y + 1 < 8:
                        image[y + 1, x + dx + 1] = 14
            for y, row in enumerate(glyph):
                for dx, pixel in enumerate(row):
                    if pixel is not None and pixel > 4 and x + dx < 152 and y < 8:
                        image[y, x + dx] = 13 if bright else 15
            x += len(glyph[0]) + 1
    # Body sprites have a guaranteed black backdrop. Removing invisible
    # black shadow texels cuts the shared tile set below 8192 (13-bit IDs).
    image[image == 14] = 0
    return image


def pack_ids(values, bits):
    result = bytearray()
    pending = count = 0
    for value in values:
        pending = (pending << bits) | value
        count += bits
        while count >= 8:
            count -= 8
            result.append((pending >> count) & 255)
        pending &= (1 << count) - 1
    if count:
        result.append(pending << (8 - count))
    return bytes(result) + b'\0\0'  # safe 24-bit extraction of the last ID


def generate(out, pack):
    glyphs = shapes.decode_sprite_table(shapes.main_shape_tables()[2])
    tile_ids, tiles, line_ids, line_maps = {}, [], {}, []
    def convert_line(text):
        image = line_bitmap(text, glyphs)
        row = []
        for x in range(19):
            cell = image[:, x * 8:x * 8 + 8]
            key = cell.tobytes()
            if key not in tile_ids:
                tile_ids[key] = len(tile_ids)
                tiles.extend(pack(cell, 1, 1))
            row.append(tile_ids[key])
        key = tuple(row)
        if key not in line_ids:
            line_ids[key] = len(line_ids)
            line_maps.extend(row)
        return line_ids[key]
    convert_line('')                 # line zero always blank
    offsets, counts, definitions, references = [], [], [], []
    cstr = lambda s: '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
    for ep in range(1, 5):
        cubes = load(ep)
        offsets.append(len(definitions))
        counts.append(len(cubes))
        for cube in cubes:
            lines = wrap(cube['body'], glyphs)
            first = len(references)
            references.extend(convert_line(line) for line in lines)
            definitions.append('{' + f'{cstr(cube["title"])},{cstr(cube["header"])},'
                               f'{first},{len(lines)},{cube["face"] + 1}' + '}')
    assert len(references) < 65536 and len(tile_ids) < 65536
    tile_bits = (len(tile_ids) - 1).bit_length()
    packed_maps = pack_ids(line_maps, tile_bits)
    header = (emit.HEADER + '#ifndef GEN_CUBES_H\n#define GEN_CUBES_H\n#include "game/types.h"\n'
              'typedef struct { const char *title, *header; uint16_t first_line; uint8_t lines, face; } CubeDef;\n'
              'extern const CubeDef cube_defs[];\n'
              'extern const uint16_t cube_offsets[4], cube_lines[];\n'
              'extern const uint8_t cube_line_maps[];\n'
              f'#define CUBE_TILE_BITS {tile_bits}\n'
              'extern const uint8_t cube_counts[4];\n#endif\n')
    source = emit.HEADER + '#include "cubes.h"\n'
    source += 'const CubeDef cube_defs[] = {' + ',\n'.join(definitions) + '};\n'
    source += emit.c_array('uint16_t', 'cube_offsets', offsets)
    source += emit.c_array('uint8_t', 'cube_counts', counts)
    source += emit.c_array('uint16_t', 'cube_lines', references)
    source += emit.c_array('uint8_t', 'cube_line_maps', list(packed_maps), 24, '{}')
    graphics = emit.HEADER + '#include "cube_graphics.h"\n'
    graphics += emit.tile_array('cube_text_tiles', tiles)
    face_defs = []
    face_palettes = (1, 2, 3, 4, 6, 9, 11, 12, 16, 13, 14, 15)
    faces = shapes.decode_sprite_table(shapes.main_shape_tables()[4])
    for index, face in enumerate(faces):
        image = np.full((56, 72), -1, np.int16)
        for y, row in enumerate(face):
            for x, pixel in enumerate(row):
                if pixel is not None:
                    image[y, x] = pixel
        rgb = np.array(palette.load_palettes()[face_palettes[index]], np.float64)
        values, ns = np.unique(image[image >= 0], return_counts=True)
        pal = quantize.quantize_cells([dict(zip(map(int, values), map(int, ns)))], rgb, n_pals=1)[0][0]
        lut = quantize.remap(np.arange(256), rgb, pal)
        pixels = np.where(image >= 0, lut[np.maximum(image, 0)], 0).astype(np.uint8)
        words = []
        for y in range(7):
            for x in range(9):
                words += pack(pixels[y*8:y*8+8, x*8:x*8+8], 1, 1)
        name = f'cube_face_{index}'
        graphics += emit.tile_array(name, words)
        graphics += emit.c_array('u16', name + '_palette', [0] + [palette.to_cram(c) for c in pal])
        face_defs.append('{' + f'{name},{name}_palette' + '}')
    graphics += 'const CubeFace cube_faces[12] = {' + ','.join(face_defs) + '};\n'
    gh = (emit.HEADER + '#ifndef GEN_CUBE_GRAPHICS_H\n#define GEN_CUBE_GRAPHICS_H\n#include <types.h>\n'
          'typedef struct { const u32 *tiles; const u16 *palette; } CubeFace;\n'
          'extern const u32 cube_text_tiles[];\nextern const CubeFace cube_faces[12];\n#endif\n')
    for name, text in [('cubes.h', header), ('cubes.c', source), ('cube_graphics.h', gh), ('cube_graphics.c', graphics)]:
        emit.write_if_changed(os.path.join(out, name), text)
    print(f'   gen: cubes: {sum(counts)} records, {len(references)} lines, {len(line_ids)} unique lines, '
          f'{len(tile_ids)} shared tiles; fixed maps {2*len(references)+len(packed_maps)} bytes, '
          f'graphics {len(tiles)*4 + len(faces)*63*32} bytes')
