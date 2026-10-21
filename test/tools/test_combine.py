"""Combined cartridge conversion keeps exported tables and internal names distinct."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
from tyrdata.combine import active_header, namespaced  # noqa: E402


def test_active_tables_preserve_pointer_constness():
    header = ('extern const Weapon weapons[781];\n'
              'extern const char *const port_names[43], *const power_names[7];\n'
              'extern const u16 *const *const big_gfx_level[];\n')
    converted = active_header(header)
    assert 'extern const Weapon  *weapons;' in converted
    assert 'extern const char *const *port_names;' in converted
    assert 'extern const char *const *power_names;' in converted
    assert 'extern const u16 *const *const *big_gfx_level;' in converted


def test_episode_symbols_do_not_change_text_or_types():
    source = ('static const uint16_t events[] = {0};\n'
              'const LevelDef levels[1] = {{ events }};\n'
              'const char *const names[] = {"events and levels"};\n')
    converted = namespaced(source, 4)
    assert 'uint16_t ep4_events[]' in converted
    assert 'LevelDef ep4_levels[1] = {{ ep4_events }}' in converted
    assert 'names[] = {"events and levels"}' in converted
    assert converted == namespaced(source, 4)


def test_episode_symbols_are_distinct():
    source = 'const uint16_t levels[] = {1,2};\n'
    assert namespaced(source, 1) != namespaced(source, 4)
    assert 'ep1_levels[]' in namespaced(source, 1)
    assert 'ep4_levels[]' in namespaced(source, 4)
