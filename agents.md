# agents.md — Tyrian MD (Tyrian → Sega Mega Drive / Genesis port)

Single source of project rules for humans and coding agents. Read it before
changing anything. Goal: a **full-speed, 60 Hz Tyrian on stock Genesis
hardware** (no Sega CD, no 32X, cartridge up to 64 Mbit / 8 MB, with mapper if needed) that
feels like Tyrian, not a demake. We adapt aggressively wherever the PC design
does not fit the machine, and we say so in code and in this file.

## Layout

```
open-tyrian/   OpenTyrian (C, GPL-2) — reference engine, read-only
tyrian-data/   Tyrian 2.1 freeware data (open-tyrian/get_data.sh) — read-only
sgdk/          SGDK 2.00 (git tag v2.00) — read-only toolchain
tools/         Python converters: PC data → Genesis assets/tables
res/           generated .res + converted assets (never hand-edit generated files)
src/           Genesis engine (C, SGDK)
platform/host/ SDL2/headless host build of the same game logic (tests, replays)
test/          permanent test suites
out/rom.bin    build output
```

Reference project with proven patterns (Makefile, bench-68k, host build,
4 MB checks, stack reserve): `~/projects/sgdk/` — reuse its tooling ideas,
not its game code. Toolchain: `m68k-elf-gcc` (Homebrew), `GDK=./sgdk`.

## The source and the target

| | PC Tyrian (OpenTyrian) | Genesis |
|---|---|---|
| CPU | 386+ (OpenTyrian: modern) | 68000 @ 7.67 MHz, ~128k cycles/frame |
| RAM | MBs | 64 KB work RAM (+ 8 KB Z80 RAM) |
| Video | 320×200, 256 colors, linear framebuffer, software blits | 320×224 (H40), 4 palettes × 15 colors + transparent, tiles 8×8, 2 scroll planes + window, 80 sprites / 20 per line, 64 KB VRAM |
| Audio | AdLib OPL2 (LDS/Loudness songs) + 8-bit PCM SFX | YM2612 (6 FM ch, OPN2) + SN76489 PSG (3 square + noise), Z80 driver, PCM on ch6 |
| Storage | ~11 MB of data files | Up to 8 MB ROM; current builds use 4 MB linear space |
| Input | keyboard/mouse/joystick | 3/6-button pad |

Engine entry points to study in `open-tyrian/src/`: `tyrian2.c` (main loop,
`JE_main`, enemy/shot update, level events), `mainint.c` (player, HUD, menus),
`backgrnd.c` (3 background layers from map tiles), `shots.c`, `varz.c/h`
(entity arrays: 100 enemies, 60 enemy shots, 81 player shots, explosions),
`lvllib.c` / `episodes.c` (level and item data), `sprite.c` (shape files),
`lds_play.c` + `opl.c` (music), `sndmast.c` (SFX).

## Rules

### §1. OpenTyrian is the behavioral reference, not the architecture

Gameplay rules (weapon tables, power levels, enemy scripts, event commands,
damage, item/shop economy, level flow) come from OpenTyrian and the original
data. Architecture (framebuffer, per-pixel blits, `double`, `malloc`, file
I/O, SDL) does **not** carry over. Port behavior; rewrite structure. When
unsure what the game does, read OpenTyrian or run it — never guess.

### §2. Data is converted at build time, never parsed at runtime

`tools/` reads `tyrian-data/*` (levels, shapes, palettes, items, music,
SFX) and emits compact ROM tables and SGDK resources. The ROM never contains
PC file formats. Rules:
- Tables are `const`, packed, struct-of-arrays where it helps the 68000
  (word-aligned, indexes ≤ 16 bit, no pointers chains to chase per frame).
- Precompute everything that is a pure function of data: palettes mapped to
  Genesis CRAM, tiles deduplicated (with H/V flip), enemy paths, weapon
  pattern tables, sine/angle LUTs, collision boxes.
- Generated files are never hand-edited; fix the converter.
- Converters are deterministic (same input → byte-identical output) and
  have tests.

### §3. Hardware budgets (MANDATORY)

- **Frame**: 60 Hz NTSC is the target, every frame, during the densest
  moment of every level. Budget per frame ≈ 120k cycles for game logic +
  VDP setup; measure, don't assume. PAL (50 Hz) must play at the same game
  speed (fixed timestep at 60, or scaled velocities — pick one, document it).
- **ROM**: up to 64 Mbit (8 MB), authorized by the user on 2026-10-21.
  The unified cartridge uses the official Sega SSF2 mapper. Code, gameplay
  tables, audio and initial RAM data must stay below physical 0x380000
  (`tools/check_cart.py` enforces it); DMA-only graphics occupy
  `.far_assets` at 0x380000 and above. The logical 0x380000–0x3FFFFF window
  maps one 512 KB graphics bank at a time, with the Z80 held during DMA.
- **RAM**: 64 KB total. Keep a budget table in this file (§9) and update it
  with every new runtime structure. SGDK stack reserve is small (≈0xA00):
  no large locals, no recursion.
- **VRAM**: 64 KB = 2048 tiles minus planes, sprite table and hscroll.
  Keep a VRAM map (§9). Streaming via DMA in vblank only; respect the DMA
  budget (~7 KB/frame in H40 vblank).
- **Sprites**: 80 hardware sprites, 20 per scanline / 320 px per line.
  Design so the worst case does not flicker gameplay-critical objects
  (player, enemy bullets).
- **Math**: no float, no `double`, no divide in hot paths. Fixed point
  (e.g. 12.4 or 8.8), LUTs for trig/atan2.

### §4. Bold adaptations are welcome — if documented

The port is allowed (and expected) to redesign subsystems for the hardware.
Each adaptation needs a comment at the code site and an entry in §8:
what changed, why, worst-case visible difference. Approved directions:

- **Resolution**: 320×200 → 320×224. Use the extra 24 lines for the HUD
  (window plane, zero CPU per frame) or a slightly taller playfield; the
  original side panel can shrink or move to make room.
- **Colors**: 256-color PC palettes → 4×15. Per-level palette quantization in
  `tools/` (background planes get 2 palettes, sprites 2; shared player/
  bullet palette fixed across the game). Mid-frame CRAM changes (H-int) are
  allowed for a richer playfield vs HUD split. Shadow/highlight mode is
  allowed for explosions/lighting if it pays visually.
- **Backgrounds**: Tyrian's 3 tile layers (back, mid, top) → Plane B (far),
  Plane A (near), and the top layer either as high-priority tiles in Plane A
  or as sprites when sparse. Vertical scrolling via VSRAM (free), column
  streaming of new tile rows by DMA. Parallax via per-plane scroll speeds;
  line/column scroll for extra effects.
- **Objects**: enemies built from 24×28 shape pieces become hardware
  sprites with pre-converted tiles; big bosses can become plane tiles drawn
  into Plane A (scrolling with the background) to save sprite slots.
- **Bullets**: enemy and player shots are the sprite budget killer. Use
  shared bullet tile sets, 8×8 / 16×16 small sprites, sprite multiplexing
  with rotating priority so flicker is spread evenly, caps lower than PC
  (keep weapon *feel*: spread, rate, damage per second).
- **Entity counts**: PC limits (100 enemies, 60 enemy shots, 81 player shots)
  may drop to what fits the frame, as long as no level becomes trivially
  easier. Record final caps in §9.
- **Music**: LDS/OPL2 songs → converted to YM2612 FM (OPL2 2-op instruments
  mapped to OPN2 4-op patches; a hand-tuned patch table is acceptable) with
  the Z80 driver (XGM2 or a custom driver). SFX: PCM on the Z80 / FM ch6,
  PSG for simple blips. Sample rates reduced to fit ROM.
- **Scope**: all episodes and features present in OpenTyrian are the target
  (user request, 2026-10-21). Start with episodes 1–4 from Tyrian 2.1 data;
  sidekicks, specials, saves, text screens and the third background layer
  remain required. Two-player mode, Destruct, networking, jukebox and
  setup/config screens come later. Additional data-dependent content needs
  its original data source; do not invent missing content.
- **Saves**: battery SRAM is allowed only if it does not shadow ROM data;
  otherwise passwords. Decide once, document in §8.

Not allowed: anything that changes level layout, enemy order, scoring/
economy rules, or weapon identities without a §8 entry approved by the user.

### §5. Game logic is portable C; hardware lives behind a thin layer

`src/game/` holds the simulation (fixed-point, deterministic, no SGDK
headers). `src/gen/` holds VDP/sprite/DMA/sound/input code. The same game
code compiles for the host (`platform/host/`) for tests, replays and fast
iteration. Same inputs ⇒ same simulation on host and ROM (bit-identical
state hashes per frame).

### §6. Performance first, measured

- Profile on the real target model: an emulator with cycle-accurate timing
  (BlastEm, or mednafen/Genesis Plus GX) plus a frame-time marker (e.g.
  background color change or a vcounter read at end of logic).
- Keep a `make bench-68k` style benchmark of the worst frame of selected
  levels; record numbers in PRs that touch hot paths.
- Hot loops in C first, measured; hand-written 68000 asm only for proven
  hot spots (collision broad phase, sprite list build, tile streaming),
  each with a C fallback for the host build.
- Rendering is work for the VDP, not the CPU: never draw pixels with the
  CPU per frame; build tiles at build time, move them with DMA.
- Do not trust host benchmarks for target performance.

### §7. Workflow

1. Read the relevant OpenTyrian code and this file.
2. Change converter or engine; keep generated files generated.
3. Run tier 0 tests; for gameplay changes, tier 1 replays.
4. Check budgets (ROM size, RAM table, worst-frame cycles).
5. Update §8/§9 when an adaptation or budget changes.
6. Commit only when asked. No temporary test scripts left behind.

Agents: do not run long sweeps (all levels × all replays) automatically;
the user runs them.

### §8. Adaptation log

Every intentional difference from OpenTyrian, one line each:
`area — change — reason — worst visible difference`.

- Timing — game logic ticks 7 times per 12 frames (35 Hz) against the PC's
  34.78 Hz (PIT 0x4300 / frameCountMax 2) — the Genesis has no 69.57 Hz
  timer and 60 Hz logic would need every speed rescaled — 0.6% faster.
  PAL runs 7 ticks per 10 display frames (35 Hz), with normalized render
  fractions and 179/256 tick velocity per frame; NTSC uses 149/256.
  Backgrounds scroll every frame, not per tick. PAL fractions are rounded
  to the renderer's 12-step interpolation grid (at most 1/24 tick error).
- Colors — PC colors are lifted by gamma 0.7 before matching, distances in
  OKLab with lightness x2 — the Genesis' darkest non-black level is 52 and
  plain nearest-color turned dark browns red — level art looks brighter than
  on PC. Measured: the 2-palette limit costs little (error 22.8 vs 21.7 with
  all 512 colors); the Genesis gamut is the floor.
- Player shots — cap 64 instead of 81 (`PLAYER_SHOT_MAX`); positions are
  interpolated between ticks for display (game state unchanged); not ported
  yet: trails, shot sounds, sparkle (gr > 1000), OPTION_SHAPES specials,
  guided aim (needs enemies), sidekicks (stage 6, with the shop: the new-game
  loadout has none). `DEBUG_WEAPONS` (Makefile `GAME_FLAGS`, on for now):
  START cycles the front weapon, A its power — remove with the shop.
- Playfield mapping — PC coordinates are kept in the simulation; the PC
  playfield (x 24..287, y 0..183) maps to screen x 8..271, y 24..207
  (offset -16, +24); the window plane frames it on the right (x >= 272) and
  bottom (y >= 208) for the HUD. Backgrounds use the PC parallax exactly
  (hscroll from mapXOfs/mapX2Ofs), so ground enemies stay on the map.
- RNG — xorshift32 instead of the Mersenne Twister (cost); same uses.
- Sprites — 2x2 enemies are composed at build time into one 24x32 sprite;
  sprites are rebuilt only on frames without a game tick and advanced by
  velocity on tick frames (objects appear/change one frame late); map-piece
  enemy frames (> 85% opaque) use the level's background palettes; all
  other enemies, items and explosions share palette line 3.
- Enemies — single player, story mode, difficulty Normal; "lives" for
  event 33 is the front weapon power, as on the PC; arcade/2-player items
  (evalue > 30000) give only their cash; specials, superbombs, data cube
  text, enemy filters, sounds and music not ported yet; explosion cap 64
  (PC 200); event 12's self-modification tracked for 16 events.
- Flow — the episode script is compiled to bytecode (tools/tyrdata/
  script.py); dropped for now: text/picture/music screens (]W ]P ]M ...),
  data cubes, galaga/extra modes (level 1.18 "ALE" never ends without
  galaga mode), two-player branches. Difficulty is fixed at Normal. Death
  replays the level with the items it started with (the PC reloads its
  level-start save). No saves across power-off yet (SRAM vs password still
  to decide).
- Shop — text screen in SMALL_FONT instead of the PC's graphical menus;
  categories front/rear/generator/shield/ship, same trade-in economy;
  sidekicks (Opt1/Opt2) not offered yet; starts on "Launch".
- HUD — window plane, TINY_FONT (dark shades dropped so glyphs stay
  legible at 4x6), bars for armor/shield/power; PAL2 indexes 14/15 are
  reserved black/white for text.
- Sound effects — XGM2 driver, 3 PCM channels instead of the PC's 8 mixed
  channels: PC channel 3 (voices) -> PCM 3 priority 15, channels 6/7
  (explosions, items) -> PCM 2, the rest -> PCM 1; effects resampled to
  6.65 kHz (ROM; 13.3 kHz for effects cost 0.17 MB). Enemy shot sounds use channel 1 instead of
  a random one (no RNG use). Launch sounds and the low-armor warning beep
  not ported. dmaq halts the Z80 during its DMAs (as SGDK's flush does).
- Music — LDS/OPL2 songs become VGM for XGM2 (`tools/music_vgm.py`, tools/tyrdata/music.py): 9 OPL voices -> 5 FM (2-op on OP1/OP2) + 3 PSG squares + PSG noise per a static per-song allocation, extra voices dropped; OPL waveforms and KSL ignored; 69.5 Hz LDS ticks quantized to 60 Hz frames by XGM2 — FM ch6 stays free for PCM SFX — timbres differ (PSG leads are square waves), 1 voice dropped in 10 of 41 songs, timing jitter <= 1 frame.
- Render scheduling — the sprite list is rebuilt only on frames without a
  game tick and, past 24 sprites, only every other such frame (advanced by
  velocity otherwise); background streaming, HUD and sound also skip tick
  frames unless a row is due. Explosion slots come from a free stack
  instead of the PC's first-free scan (visual only).
- HUD bottom bar — event 16 window texts (100 simulation ticks, as on PC)
  and the boss bar (event 79,
  most damaged linked enemy, as draw_boss_bar) instead of the PC's top bar.
- Title screen — "Tyrian MD": the PC title (pic 4 + PLANET_SHAPES logo)
  redrawn with "MD" via Gemini image generation (res/art/title_md.png,
  converted by gen_title), credits: Mega Drive version by Jonathan Santos,
  Jason Emery, Daniel Cook, Alexander Brandon, Andreas Molnar, Epic
  MegaGames. SMALL_FONT's 12-px M/W are squeezed to 8 px.
- Sprite cache — keep 20 big-enemy slots and fit up to 88 small-piece
  slots into VRAM left by the current level (48 at episode 2's maximum) — its
  backgrounds need 951 resident tiles — extra distinct pieces can be
  omitted during a cache-saturated rebuild; ship/enemy bullets keep draw
  priority. Simulation and collision caps are unchanged.
- Episode packaging — all four episodes in one 8 MB SSF2 cartridge,
  text episode selector and equipment/cash carried across episodes, including
  the original SuperCarrot lottery on wrap — fit stock hardware — episode
  completion still uses simple text screens; credits/cutscenes remain pending.
- Emulator launcher — Mednafen 1.32 uses a product-code lookup for SSF2,
  ignoring the homebrew `SEGA SSF` console header; `play.sh` prepares a copy
  with the SSF2 database ID — keep the hardware ROM's Tyrian identity —
  only the emulator copy reports that compatibility product ID.
- Backgrounds — layer 3 (top) not drawn yet; layers are 336 px wide and are
  centered (8 px cut each side) instead of following the player's X — PC
  shows 264 px of them.

### §9. Budgets

Fill and keep current.

- RAM: (struct — bytes — notes)
  - `bg.c cell_tables` — 4 bytes per background cell of the level (5,816 for
    1.9) — VRAM slot + reference count per cell.
  - `bg.c free_slots` — 2 bytes per pool slot (1,368 for 1.9).
  - `player_shots` — 64 x 40 bytes = 2,560.
  - `enemies` — 100 x 124 bytes = 12,400 (largest structure; trim later).
  - `enemy_shots` 1,440, `explosions` 1,024, `gfx_slot` 2,032 (one byte per
    sprite piece of the level), shot collision lists ~300.
  - total .bss depends on the episode: episode 1 34,372 bytes; episode 2
    34,980; episode 3 34,364; episode 4 30,124 (historical separate builds).
    Unified cartridge: .bss 35,592 bytes + .data 90; active table pointers
    133 bytes, two campaign-state bytes, dynamic sprite-slot count 2 bytes,
    PAL sprite velocity scale 2 bytes; HUD text uses a 32-bit tick deadline.
    Normal boot uses 1,492 stack bytes, below the 0xA00 reserve (2026-10-21).
  - `sprites.c` piece cache — up to 88 slots x 4 bytes (48 for episode 2);
    `debug_hold_level` — 1 byte for headless replay flow control.
- VRAM map: (region — tiles/bytes)
  - tiles 0-15 SGDK system; 16.. background slot pool, sized per level by
    `BG_POOL_NEED` (684 for 1.9: worst 30-row window, both layers); then free
    for sprites: ship 12 tiles, piece cache up to 88 x 4 = 352 tiles, big-enemy
    cache 20 x 12 = 240 tiles (`SPRITES_VRAM_TILES`); HUD bars 28 tiles;
    the HUD font replaces SGDK's font (96 tiles below 0xC000). RAM pool sized for
    the largest level (951); VRAM reserves only the current level's pool,
    and the small-piece cache shrinks to fit. main.c has a
    `_Static_assert` on the total (1440) and the tilemaps at 0xC000 (SGDK
    defaults: plane B 0xC000, window 0xD000, plane A 0xE000, sprites 0xF800).
- Entity caps: enemies 100, enemy shots 60, player shots 64, explosions 64.
- ROM: unified cartridge (2026-10-21): fixed .text ~3.24 MB,
  initial .data 90; far graphics 3,487,232; physical end 0x6D3600 (~7.16 MB),
  padded to 8 MB (85%). Fixed-region headroom is ~420 KB. The shipped SGDK math source is
  recompiled with function sections (read-only include); garbage collection
  removes ~393 KB of unused log/sqrt tables and other unused math routines.
  Further code/tables still need measured budgets. Shared episode 1–3 item tables
  are emitted once. Audio stays fixed, including the Z80's ROM accesses.
  - separate-episode builds (2026-10-21): episode 1 ~3.16 MB, episode 2
    ~2.75 MB, episode 3 ~2.90 MB, episode 4 ~2.95 MB; each is padded to 4 MB. These are opening
    replay checks, not full-campaign completion or visual verification.
  - episode 1 (18 levels, sprites of its 17 banks, script, shop,
    fonts): 2.82 MB; + 38 sound effects/voices (XGM2 PCM) 0.40 MB = 3.23 MB
    of 4 MB; + 41 songs (XGM2) 0.54 MB, sprite pieces limited to the drawn
    ones (-0.5 MB), effects at 6.65 kHz: 3.12 MB (74%) (2026-10-18).
    Episodes 2-4 will need
    compression (APLIB/LZ4W tiles decompressed per level) or trimming.
- Backgrounds (`python3 tools/budget.py`, 2026-10-18, before palette
  quantization): unique 8x8 cells deduplicated per tile file = 23,489
  (734 KB raw 4bpp; z 7296, x 5405, w 4596, ) 3295, y 2897). Per level
  (all 3 layers, H/V flips merged): worst 2,505 cells (1.9 = 1.15 = 3.8),
  so backgrounds must stream cells into VRAM while scrolling; 7 levels have
  no background (starfield/bonus). Colors per level up to 137 (1.9),
  against 45-60 on screen: palette quantization is the main quality risk.
  1,884 of the cells have more than 15 colors on their own.
- Background layout plan (stage 3): 24x28 map tiles do not align with
  8-px cells (28 = 3.5 cells), but every 2 map rows (56 px) the grid
  realigns. Per tile file: even-row cells (rows 0-23) and odd-row cells
  (rows 4-27) per tile, plus per level a table of the "mixed" cell row
  (bottom 4 px of tile A + top 4 px of tile B) for each vertical pair that
  occurs (max 515 pairs per layer). Maps stay in tile units.
- Worst frame (cycles): (level — cycles — date)
  - PAL episode 2 opening, 600 ticks: 872 game-loop frames (858.6 ideal),
    avg busy 84,295 / max 151,518 of 152,256 cycles; stack 1,264 bytes.
    This short run has no frame without idle, but some loop work spans
    vblank; full-level timing remains unverified (2026-10-21).
  - unified ROM, 600-tick opening replay, 2026-10-21: episode 2 avg busy
    80,008 / max 127,856 cycles, 24 frames without idle; episode 4 (Normal
    selected, Easy internally as on PC) 66,058 / 118,516, none without idle.
    Normal title/episode-menu boot reaches the episode 1 golden hash and
    uses 1,492 stack bytes. Short profiles do not prove worst-case 60 Hz.
  - short opening replays, 600 ticks, 2026-10-21 (no level restart after
    death): episode 2 avg busy 79,623 / max 127,856 cycles, 24 frames
    without idle (before starting-funds correction); episode 3 83,553 / 127,856, 13 frames without idle;
    episode 4 66,499 / 119,884, none without idle. These are smoke profiles,
    not worst-case bounds: measured loop iterations can span a vblank.
    Musashi does not model DMA/FIFO stalls or Z80 timing.
  - stage 8, `make bench-levels` (2000 ticks, constant fire + weapon
    sweep, ideal 3429 frames): level 1 3765 (+9.8%), 10 3648 (+6.4%),
    14 3602, 4 3589, 3 3619, 18 3544, the rest within +3% (2026-10-18).
    Heavy scenes slow down a little instead of dropping frames (as many
    Genesis shooters do). Logic is ~80k cycles per tick in level 1, spread
    over many functions (enemies_update ~34k): the next big lever is
    68000 assembly for enemies_update / shot collisions, or trimming Enemy.
  - stage 5 level 1.9 with enemies, firing + weapon cycling replay — avg
    68.7k (54%), max 125k (98%), no frame over budget (600 ticks in 1034
    frames, 1028 ideal) — 2026-10-18. Little margin left: next levers are
    trimming `Enemy`, cheaper `enemies_update`, and the sprite build.
  - stage 4 level 1.9, firing + weapon cycling replay — avg 28.6k (22%),
    max 71k on row-streaming frames — 2026-10-18. Half of a spike is SGDK's
    `DMA_queueDma` (~700 cycles per entry, one per new background tile): a
    leaner DMA queue is the next lever.
  - stage 3 level 1.9 scrolling (2 layers) — avg 14.0k, max 65.6k on the
    frames that upload a background row (one row every ~7 frames) —
    2026-10-18. Row uploads now split over 3 frames (stage 4).
  - stage 1 boot loop — avg 7.7k / max 64k (boot frame) of 127.9k, deepest
    stack 976 bytes of 0xA00 — 2026-10-18 (`make bench-68k`). `game_hash()`
    (FNV, 32-bit multiply) costs ~2.6k cycles/frame: it is a test hook, move
    it behind a build flag once frames get tight.

## Tests

- **Tier 0 (every edit, seconds)**: `make test` = host unit tests
  (`test/test_game.c`), ROM build (fails above 8 MB, fixed region checked, pads, checksum) and
  `make parity`: the ROM runs headless on Musashi (`tools/bench68k`) and must
  reach the same `frame_hash` as `out/tyrian_host` on `test/replays/boot.txt`
  (`BENCH_TICKS`, `REPLAY` override). bench68k also aborts on CPU
  exceptions, CPU reads outside the logical 4 MB cartridge window, mapper
  reads past physical ROM, DMA source boundary errors and stack beyond
  SGDK's 0xA00 reserve. All four episode opening replays run in tier 0,
  in both NTSC and PAL, together with a synthetic test of all 16 physical
  mapper banks and a host test that both clocks reach the same state after
  five seconds.
  Converter unit tests (Python) join tier 0 from stage 2. The runner sets
  `debug_hold_level` at the first tick: death/end does not restart or leave
  the level during a fixed-length replay, matching the host. The default
  600-tick boot replay also checks `test/replays/boot.hashes` by episode.
  Normal game flow is unchanged. Performance results after death must be interpreted
  accordingly (no live-player restart).
- **Setup (once)**: `make setup-sgdk` strips LTO IR from `sgdk/lib/libmd.a`
  and LTO flags from `sgdk/makefile.gen` (GCC 15 rejects SGDK 2.00's LTO).
- **Hooks the tools rely on** (do not rename): `game_tick(u16 pad)` stays
  `noinline` (bench68k counts ticks and writes the replay pad into its
  stack argument), `frame_hash` holds `game_hash()` after each tick.
- **Tier 1 (gameplay changes)**: recorded input replays on the host build
  compared to golden state hashes; the same replays on the ROM (headless
  emulator) must produce the same hashes.
- **Tier 2 (manual)**: full-episode replays, real-hardware run (flashcart),
  per-level worst-frame profiling.

Write tests for behavior, not implementation; no duplicate tests.

## Roadmap

1. **Toolchain**: Makefile with `GDK=./sgdk`, empty ROM boots, size/stack
   checks, host build skeleton.
2. **Data converters** (readers done: `tools/tyrdata/`, formats in
   `docs/data_formats.md`; Genesis emitters land with their consumer
   stage): palettes, shape files (`*.shp`, `newsh*.shp`),
   level maps and events (`tyrian*.lvl`, `levels*.dat`), item tables
   (`tyrian.hdt`), text.
3. **Background** (done for level 1.9, layers 1-2: `tools/gen.py`
   `gen_level_bg`, `src/bg.c`): one level scrolling with 2 planes at 60 Hz,
   tile streaming, palette quantization review.
4. **Player + weapons** (done except sidekicks: `src/game/player.c`,
   `shots.c`, `src/sprites.c`; `make rom-demo` plays itself): ship, movement, front/rear weapons, power levels,
   sidekicks; shots as sprites with multiplexing.
5. **Enemies + events** (done for one level: `src/game/enemy.c`,
   `level.c`; HUD values exist but are not drawn): level event interpreter, enemy movement/AI,
   collisions, explosions, items, score.
6. **HUD + flow** (done for episode 1 except saves, text screens and
   sidekicks: `src/game/script.c`, `shop.c`, `src/menu.c`, `src/hud.c`):
   window-plane HUD, level end, shop/upgrade screen, episode progression,
   saves.
7. **Audio** (done: `tools/lds_dump`, `tools/tyrdata/music.py`,
   `make music`, `src/sound.c`): OPL→YM2612 music conversion, SFX.
8. **Bosses, polish, worst-frame tuning** (done: boss bar, window texts,
   shop music, `make bench-levels`; bosses verified killable on the host;
   slowdown documented in §9).
9. Optional: 2-player, Destruct, extras.

## Licensing

OpenTyrian is GPL-2: derived engine code in this repo is GPL-2. Tyrian 2.1
data is freeware (Jason Emery), graphics released by Daniel Cook — keep the
data-derived ROM non-commercial and credit both. Do not commit
`tyrian-data/`.
