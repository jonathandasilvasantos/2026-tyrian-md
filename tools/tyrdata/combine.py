"""Link all four episodes, keeping CPU tables fixed and DMA graphics far.

Each episode is converted independently to preserve its palettes and item
records. Public arrays get an episode prefix; the engine uses direct active
pointers so hot loops never chase an episode descriptor.
"""
import hashlib
import os
from pathlib import Path
import re

from . import DATA_DIR, emit, level
from .script import help_section

PORTABLE = ("items", "level", "script")
GRAPHICS = ("level_bg", "sprites")
MODULES = PORTABLE + GRAPHICS
EXTERN = re.compile(r"extern (const [^;]+);", re.M)
ARRAY = re.compile(r"^(.*?)([A-Za-z_]\w*)\s*\[[^]]*\]$")
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\b[A-Za-z_]\w*\b')


def arrays(header):
    """(base type, declarator prefix, name) for every exported array."""
    result = []
    for match in EXTERN.finditer(header):
        parts = match[1].split(",")
        first = ARRAY.match(parts[0].strip())
        if not first:
            continue
        prefix, name = first.groups()
        # A base type precedes any pointer declarators.
        base = prefix.split("*", 1)[0].strip()
        result.append((base, prefix[len(base):].strip(), name))
        for part in parts[1:]:
            prefix, name = ARRAY.match(part.strip()).groups()
            result.append((base, prefix.strip(), name))
    return result


def pointer_declaration(base, prefix, name):
    return f"{base} {prefix} *{name}"


def active_header(header):
    def replace(match):
        entries = arrays(match[0])
        if not entries:
            return match[0]
        return "\n".join("extern " + pointer_declaration(*entry) + ";" for entry in entries)
    return EXTERN.sub(replace, header)


def namespaced(source, episode):
    names = set(re.findall(r"^(?:static )?const [^\n=]+?\b([A-Za-z_]\w*)\s*\[", source, re.M))
    return TOKEN.sub(lambda m: f"ep{episode}_{m[0]}" if m[0] in names else m[0], source)


def fingerprint():
    digest = hashlib.sha256()
    paths = sorted(Path(DATA_DIR).glob("*")) + [Path("tools/gen.py")]
    paths += sorted(p for p in Path("tools/tyrdata").glob("*.py") if p.name != "combine.py")
    for path in paths:
        if path.is_file():
            digest.update(str(path).encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()


def generate_episodes(out, default, gen_bg, write_bgs, gen_levels, gen_script, gen_items, gen_sprites):
    cache = Path("out/episode-cache")
    digest = fingerprint()
    headers = {}
    for ep in range(1, 5):
        target = cache / str(ep)
        stamp = target / "fingerprint"
        if not stamp.exists() or stamp.read_text() != digest:
            nums = list(range(1, level.level_count(ep) + 1))
            bgs = [gen_bg(ep, n, i) for i, n in enumerate(nums)]
            write_bgs(str(target), bgs)
            gen_levels(str(target), ep, nums)
            gen_script(str(target), ep)
            gen_sprites(str(target), gen_items(str(target), ep),
                        [level.load_level(ep, n) for n in nums], [b["pals"] for b in bgs])
            emit.write_if_changed(str(stamp), digest)
        headers[ep] = {m: (target / f"{m}.h").read_text() for m in MODULES}
    canonical = {}
    for module in MODULES:
        seen = {}
        for ep in range(1, 5):
            content = (cache / str(ep) / f"{module}.c").read_text()
            canonical[module, ep] = seen.setdefault(content, ep)
        header = active_header(headers[1][module])
        if module == "level":
            for macro, var in (("LEVEL_COUNT", "level_count"), ("LEVEL_FIRST", "level_first"),
                               ("LEVEL_EPISODE", "current_episode")):
                header = re.sub(rf"#define {macro} \d+", f"#define {macro} {var}\nextern uint8_t {var};", header)
            header = header.replace("#endif", f"#define LEVEL_DEFAULT_EPISODE {default}\n#endif")
        elif module == "script":
            for macro, var in (("SCRIPT_SECTIONS", "script_section_count"), ("SHOP_COUNT", "shop_count")):
                header = re.sub(rf"#define {macro} \d+", f"#define {macro} {var}\nextern uint8_t {var};", header)
        for macro in ("BG_POOL_NEED", "BG_CELLS_TOTAL", "SPR_GFX_COUNT", "BIG_GFX_COUNT"):
            values = [re.search(rf"#define {macro} (\d+)", headers[ep][module]) for ep in range(1, 5)]
            if values[0]:
                header = re.sub(rf"#define {macro} \d+", f"#define {macro} {max(int(v[1]) for v in values)}", header)
        emit.write_if_changed(os.path.join(out, module + ".h"), header)
        sources = []
        for ep in range(1, 5):
            if canonical[module, ep] != ep:
                continue
            text = (cache / str(ep) / f"{module}.c").read_text()
            for macro in ("LEVEL_COUNT", "SHOP_COUNT", "SCRIPT_SECTIONS"):
                value = re.search(rf"#define {macro} (\d+)", headers[ep][module])
                if value:
                    text = re.sub(rf"\b{macro}\b", value[1], text)
            sources.append(namespaced(text, ep))
        source = "\n".join(sources)
        emit.write_if_changed(os.path.join(out, module + ".c"), source)
    for kind, modules in (("tables", PORTABLE), ("graphics", GRAPHICS)):
        source = emit.HEADER + '#include "episodes.h"\n'
        source += "".join(f'#include "{m}.h"\n' for m in modules)
        entries = [entry for m in modules for entry in arrays(headers[1][m])]
        for base, prefix, name in entries:
            source += pointer_declaration(base, prefix, name) + ";\n"
            for ep in range(1, 5):
                source += f"extern {base} {prefix} ep{ep}_{name}[];\n"
        if kind == "tables":
            names = help_section("** Section 16 ")[1:5]
            source += "const char *const episode_names[4] = {" + ",".join('"' + n.strip() + '"' for n in names) + "};\n"
            source += "uint8_t current_episode, level_count, level_first, script_section_count, shop_count;\n"
        source += f"void episode_{kind}_select(uint8_t ep)\n{{\n    switch (ep) {{\n"
        for ep in range(1, 5):
            source += f"    case {ep}:\n"
            for module in modules:
                for _, _, name in arrays(headers[1][module]):
                    source += f"        {name} = ep{canonical[module, ep]}_{name};\n"
            if kind == "tables":
                for macro, var, module in (("LEVEL_COUNT", "level_count", "level"), ("LEVEL_FIRST", "level_first", "level"),
                                           ("SCRIPT_SECTIONS", "script_section_count", "script"), ("SHOP_COUNT", "shop_count", "script")):
                    value = re.search(rf"#define {macro} (\d+)", headers[ep][module])[1]
                    source += f"        {var} = {value};\n"
                source += f"        current_episode = {ep};\n"
            source += "        break;\n"
        source += "    }\n}\n"
        emit.write_if_changed(os.path.join(out, f"episode_{kind}.c"), source)
    emit.write_if_changed(os.path.join(out, "episodes.h"), emit.HEADER +
                         '#ifndef GEN_EPISODES_H\n#define GEN_EPISODES_H\n#include "game/types.h"\n'
                         '#define EPISODE_COUNT 4\nextern const char *const episode_names[4];\nvoid episode_tables_select(uint8_t ep);\n'
                         'void episode_graphics_select(uint8_t ep);\n#endif\n')
