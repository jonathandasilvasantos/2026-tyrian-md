# Tyrian MD

**Tyrian for the Sega Mega Drive / Genesis — Mega Drive version by Jonathan Santos.**

A port of **Tyrian** (1995, Epic MegaGames) to stock Mega Drive hardware: 68000
@ 7.67 MHz, 64 KB RAM, cartridge budget up to 64 Mbit (8 MB), no add-ons.
Current builds contain one episode in a 4 MB cartridge. Built with SGDK 2.00 from
the freeware Tyrian 2.1 data and the [OpenTyrian](https://github.com/opentyrian/opentyrian)
source as the behavioral reference.

**Status:** episode 1 is playable end to end — 18 levels, the original
episode script (shops, destination choice, level flow), enemies and level
events ported line by line, weapons and generator power, HUD, shop with the
original trade-in economy, the 41 songs converted to YM2612/PSG, sound
effects and voices. See `agents.md` §8 for every deviation from the PC game
and §9 for budgets.

Not yet: saves, text screens and data cubes, sidekicks and special weapons,
the third (top) background layer, combined episode selection/progression.
Episodes 2–4 can be built separately; their full playthroughs and visual
accuracy still need validation. A combined 8 MB ROM needs cartridge mapping.

## Quick start

Requirements (macOS/Homebrew; Linux equivalents work): `m68k-elf-gcc`, Java
(rescomp), Python 3 with numpy, pytest and Pillow, SDL2 headers (music dump
tool), `mednafen` to play.

```sh
git clone --recurse-submodules git@github.com:jonathandasilvasantos/2026-tyrian-md.git
cd 2026-tyrian-md
git clone --depth 1 --branch v2.00 https://github.com/Stephane-D/SGDK.git sgdk
make setup-sgdk      # patches SGDK 2.00's prebuilt lib for GCC 15
make rom             # episode 1: converts data and builds out/rom.bin
make rom EPISODE=2   # select episode 1, 2, 3 or 4 (overwrites out/rom.bin)
./play.sh            # fullscreen in mednafen (./play.sh demo: self-playing ROM)
```

`make rom` downloads Tyrian 2.1 (freeware) into `tyrian-data/` on first use
and converts everything (`make gen`, `make music`); nothing from the original
game is stored in this repository.

Controls: B fire, C rear weapon mode. Temporary debug keys until the shop
sells everything: START cycles the front weapon, A its power.

## Layout

| Path | What |
|---|---|
| `src/game/` | portable simulation (enemies, events, weapons, script, shop) — compiles for the ROM and the host |
| `src/` | Mega Drive side: streaming backgrounds, sprite list, DMA queue, HUD, menus, sound |
| `tools/` | Python converters (`tyrdata/`), `gen.py`, music converter, 68000 benchmark (`bench68k/`) |
| `platform/host/` | host build of the simulation (replays, parity) |
| `test/` | unit tests, converter tests, replays |
| `docs/data_formats.md` | Tyrian PC data formats |
| `agents.md` | project rules, adaptations log, budgets, roadmap |
| `open-tyrian/` | OpenTyrian (submodule, read-only reference) |

## Tests

```sh
make test          # episode 1: converter + host tests, ROM build, parity
make test EPISODE=2 # the same checks for a selected episode
make bench-68k     # CPU load, deepest stack, per-function profile
make bench-levels  # frames used per level (manual, minutes)
```

`make parity` runs the ROM headless on a 68000 emulator (Musashi) and checks it
reaches the same game state as the host build on the same input replay.

## Credits

- Mega Drive version: Jonathan Santos
- Tyrian (1995, Epic MegaGames): Jason Emery (programming, design), Daniel Cook
  (artwork), Alexander Brandon (music, story), Andreas Molnar (LOUDNESS sound
  system, music) and the rest of the team listed in the game's credits
- Title art: the original title screen redrawn with "MD" (generated with
  Google Gemini image generation from the PC title, `res/art/title_md.png`)

## Licenses

Code derived from OpenTyrian is GPL-2.0. Tyrian 2.1 data is freeware by Jason
Emery; graphics by Daniel Cook. Non-commercial fan project.
