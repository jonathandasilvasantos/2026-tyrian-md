# Tyrian MD

**Tyrian for the Sega Mega Drive / Genesis — Mega Drive version by Jonathan Santos.**

A port of **Tyrian** (1995, Epic MegaGames) to stock Mega Drive hardware: 68000
@ 7.67 MHz, 64 KB RAM, cartridge budget up to 64 Mbit (8 MB), no add-ons.
The unified 8 MB SSF2 cartridge contains all four Tyrian 2.1 episodes. Built with SGDK 2.00 from
the freeware Tyrian 2.1 data and the [OpenTyrian](https://github.com/opentyrian/opentyrian)
source as the behavioral reference.

**Status:** one ROM contains all four Tyrian 2.1 episodes (62 levels),
with episode selection and campaign progression that preserves equipment
and cash. `EPISODE=1..4` sets the menu default and benchmark episode. The engine includes episode
scripts, shops and destination choice, enemies and level events, front/rear
weapons, equipped story specials and original ship combos, superbombs and asteroid-killer pickups, the 30 sidekicks (shop, movement, charge/ammo, HUD), generator power, HUD, the 41 songs converted to YM2612/PSG, sound
effects and voices. Story starting funds follow the original episode; episode 4 also applies
the original difficulty reduction. Six story difficulty choices, enemy scaling,
score-based difficulty progression, starfields/Astral backgrounds, event/flare
palette effects, the original story text/pictures/music cues, datacube reading with
portraits and campaign availability, START pause and shop checkpoint passwords
are implemented.

Validation covers script references for every episode, host unit tests and
600-tick ROM/host replays at each episode's opening level on both NTSC and
PAL. Both regions run the same 35 Hz simulation. Full campaigns,
visual accuracy and hardware timing still need validation; dense scenes can
slow down. See `agents.md` §8 for deviations and §9 for budgets.

Still required: story text glow/picture pans, ending animation, the third
(top) background layer, and the remaining OpenTyrian modes/features. The
ROM uses the official Sega SSF2 mapper; code, tables and audio stay in the
fixed lower 3.5 MB, while DMA maps graphics through the upper 512 KB.

## Quick start

Requirements (macOS/Homebrew; Linux equivalents work): `m68k-elf-gcc`, Java
(rescomp), Python 3 with numpy, pytest and Pillow, SDL2 headers (music dump
tool), `mednafen` to play.

```sh
git clone --recurse-submodules git@github.com:jonathandasilvasantos/2026-tyrian-md.git
cd 2026-tyrian-md
git clone --depth 1 --branch v2.00 https://github.com/Stephane-D/SGDK.git sgdk
make setup-sgdk      # patches SGDK 2.00's prebuilt lib for GCC 15
make rom             # all episodes: builds the unified out/rom.bin
make rom EPISODE=2   # same cartridge, episode 2 selected by default
./play.sh            # fullscreen in mednafen (./play.sh demo: self-playing ROM)
```

`make rom` downloads Tyrian 2.1 (freeware) into `tyrian-data/` on first use
and converts everything (`make gen`, `make music`); nothing from the original
game is stored in this repository.

`./play.sh` creates `out/rom-mednafen.bin` for Mednafen 1.32, whose
[cartridge database](https://github.com/libretro-mirrors/mednafen-git/blob/master/src/md/cart/cart.cpp)
selects SSF2 by product code rather than the standard `SEGA SSF` header.
Only that emulator copy changes the product code; `out/rom.bin` retains
Tyrian's identity for flashcarts and emulators supporting the SSF header.

Controls: B fires the equipped weapons and infinite-ammo sidekicks, C
changes rear weapon mode, A fires the left limited-ammo sidekick, and Z
fires the right one on a 6-button pad. On a 3-button pad A fires both
limited-ammo sidekicks. Z (A on a 3-button pad) launches/recalls the right
front-mounted attachment. A/Z also release held superbombs; START pauses
and resumes. C in the shop displays a checkpoint password; select
Continue on the title menu to enter it (L/R cursor, up/down symbol, B next,
A/START load, C cancel). The 48-character code stores equipment, cash,
difficulty and episode progress, with a checksum for transcription errors.
Ship combos use the original directional sequences and B. The Talon
invulnerability combo is down, up, down, up+B and costs all shield (minimum
four points). The old weapon/power debug shortcuts are removed.

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
make test          # converters, all four episode unit tests/replays, mapper
make parity EPISODE=2 # one episode against its golden replay
make bench-68k     # CPU load, deepest stack, per-function profile
make bench-levels  # frames used per level (manual, minutes)
```

`make parity-sidekicks` checks six companion loadouts, including charge,
finite ammo, orbiting satellites, both companion sprite sizes and Plasma
Storm's large artwork/collision bounds, plus two superbomb replays.
`make parity-specials` checks eight special loadouts in PAL and NTSC through
`make test`; their cooldown, pulse, flare and projectile states are hashed. The player/bullet palette stays
identical across the four episodes. Star simulation and its initialization
RNG calls are checked on both CPUs; the renderer uses spare sprite slots
after gameplay objects and coarse background occlusion.

`make parity` runs the ROM headless on a 68000 emulator (Musashi) and checks it
reaches the same game state as the host build on the same input replay.
The default 600-tick replay must also match the episode hash in
`test/replays/boot.hashes`. The runner holds the current level after death
or completion so both builds compare the same ticks.

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
