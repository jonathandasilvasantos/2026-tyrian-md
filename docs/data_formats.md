# Tyrian 2.1 PC data formats (from OpenTyrian source)

Source: `open-tyrian/src/` (citations are `file.c:function`). Data: `tyrian-data/`.
All sizes below were checked against the actual files with Python (see "Verification").

**General conventions** (`file.h`): every integer is **little-endian** unless marked BE.
`fileReadBool` = 1 byte (nonzero = true). Signed types: `JE_shortint`=s8, `JE_integer`=s16,
`JE_word`=u16, `JE_byte`=u8. Pascal strings = 1 length byte + chars.
"Offset table" files all share one header: `u16 count; u32 offset[count]` (absolute file offsets),
entry *i* spans `offset[i] .. offset[i+1]` (or EOF for the last).

---

## 1. Level files `tyrian1..4.lvl`

### 1.1 Directory — `lvllib.c:analyzeLevel`
| off | type | field |
|---|---|---|
| 0 | u16 | `lvlNum` (number of entries) |
| 2 | u32[lvlNum] | `lvlPos[]` absolute offsets |

Entries come in **pairs per level**: `lvlPos[2*(n-1)]` = level header+events, `lvlPos[2*(n-1)+1]` = map block
(always exactly 22368 bytes). The engine seeks only to the even entry (`tyrian2.c:JE_loadMap`,
`fileSetPosition(lvlPos[(lvlFileNum-1)*2])`) and reads sequentially; the two parts are contiguous.
`lvlFileNum` comes from the `]L[` script command (§2).

| file | size | lvlNum | levels | last entry |
|---|---|---|---|---|
| tyrian1.lvl | 538262 | 37 | 18 | `lvlPos[36]` = EOF (unused) |
| tyrian2.lvl | 381242 | 25 | 12 | `lvlPos[24]` = EOF |
| tyrian3.lvl | 393398 | 25 | 12 | `lvlPos[24]` = EOF |
| tyrian4.lvl | 800006 | 41 | 20 | `lvlPos[40]` = **episode-4 item data** (137192 bytes, same layout as §4 item block; `episodes.c:JE_loadItemDat` uses `lvlPos[lvlNum-1]` when `episodeNum > 3`) |

### 1.2 Level header + events (at `lvlPos[2k]`) — `tyrian2.c:JE_loadMap`
| type | field | notes |
|---|---|---|
| char | `char_mapFile` | read and **ignored** (`(void)char_mapFile`) — observed values e.g. 'E','D','0'… |
| char | `char_shapeFile` | tile file = `shapes%c.dat` with `tolower()`; observed: 'Z','Y','X','W',')' |
| u16 | `mapX` | layer-1 horizontal start column (used for enemy X: `ex = eventdat2 - (mapX-1)*24`) — always 1 except see below |
| u16 | `mapX2` | layer-2 X start |
| u16 | `mapX3` | layer-3 X start |
| u16 | `levelEnemyMax` | ≤ 40 |
| u16[levelEnemyMax] | `levelEnemy[]` | enemy ids (index into enemyDat) spawned randomly while `enemiesActive` (`tyrian2.c` main loop: `levelEnemy[rand % levelEnemyMax]` when `rand%100 > levelEnemyFrequency`) |
| u16 | `maxEvent` | must be < COUNTOF(eventRec) |
| 11 B × maxEvent | events | see 1.3 |

Header size = `8 + 2 + 2*levelEnemyMax + 2 + 11*maxEvent` = `lvlPos[2k+1]-lvlPos[2k]` exactly (verified for all levels).

### 1.3 Event record (11 bytes) — `JE_loadMap`, `varz.h:struct JE_EventRecType`
**Note the on-disk field order differs from the struct order** (dat5/dat6 before dat4):
| off | type | field |
|---|---|---|
| 0 | u16 | eventtime (compared to `curLoc`, the scroll position counter) |
| 2 | u8 | eventtype |
| 3 | s16 | eventdat |
| 5 | s16 | eventdat2 |
| 7 | s8 | eventdat3 |
| 8 | s8 | eventdat5 |
| 9 | s8 | eventdat6 |
| 10 | u8 | eventdat4 |

After loading the engine sets `eventRec[maxEvent].eventtime = 65500` as a sentinel.

### 1.4 Map block (at `lvlPos[2k+1]`, 22368 bytes) — `JE_loadMap`
| off | size | content |
|---|---|---|
| 0 | 3×128 **u16 BE** (`fileReadU16BEArray`) | `mapSh[layer][0..127]`: per-layer lookup: local tile index → shapes.dat record number (1-based, 0 = none). Only indices 0..71 are used. |
| 768 | 14×300 u8 | layer 1 map, row-major, 300 rows × 14 cols |
| 4968 | 14×600 u8 | layer 2 map, 600 × 14 |
| 13368 | 15×600 u8 | layer 3 map, 600 × 15 |

Each map byte is a local index `x` → tile `mapSh[layer][x]`. Layer capacities (`varz.h` megaData1/2/3):
layer 1: 72 tiles (0..71), always opaque (`backgrnd.c:blitBackground1Row` memcpy, blank tile = zeros);
layer 2: 71 tiles, index 71 or a blank tile → NULL (not drawn); layer 3: 70 tiles, index ≥70 or blank → NULL.
Layers 2/3 are drawn with transparency (`blitBackgroundRow`; pixel 0 = transparent — inferred from the
`fill` flag computation: `fill=1` iff the first 336 bytes contain no 0 — note it only checks the first half
of the tile, a quirk of the Pascal WORD port).
Scrolling starts at the bottom: `mapY=300-8`, `mapY2=mapY3=600-8` (`tyrian2.c:JE_main`, ~line 722), and moves
upward. Visible area is 264×184 at x=24; rows of 12 tiles are drawn (`backgrnd.c`).
Unsure: exact horizontal scrolling / which 12 of 14–15 columns are visible depends on player X (`mapXOfs` etc.), not data.

### 1.5 Tile file `shapes?.dat` — `JE_loadMap`
Exactly 600 records read sequentially (record number = index+1):
`u8 blank; if blank==0: u8 pixels[24*28]` (672 bytes, row-major 24 wide × 28 tall, 8-bit palette indices).
Every file has **520 trailing bytes** never read.

| file | size | non-blank | trailing |
|---|---|---|---|
| shapes).dat | 118720 | 175 | 520 |
| shapesw.dat | 172480 | 255 | 520 |
| shapesx.dat | 217504 | 322 | 520 |
| shapesy.dat | 152992 | 226 | 520 |
| shapesz.dat | 243712 | 361 | 520 |

Note: tyrian4's `lvlPos[40]` is item data, not a level (parsing it as one yields garbage).

### 1.6 Event types — `tyrian2.c:JE_eventSystem`
"spawn(ofs)" = `JE_createNewEventEnemy(typeOfs, ofs, …)`: enemy id = `eventdat(+typeOfs)`, x = `eventdat2` (−99 = keep enemyDat start pos) minus layer offset, `ey += dat5`, `eyc += dat3`, `linknum = dat4`, `fixedmovey = dat6`. Enemy slot groups: 0–24 sky (ofs 0), 25–49 ground (25), 50–74 top/layer3 (50), 75–99 ground2 (75).
"link" = applies to enemies whose `linknum == dat4` (0 or 99 usually = all; dat3 80..89 → use `newPL[dat3-80]`).

| id | meaning |
|---|---|
| 1 | starfield speed = dat |
| 2 | set scroll speeds: backMove=dat, backMove2=dat2, backMove3=dat3 (delays reset to 1) |
| 3 | preset slow scroll (1/3, 1/2, 1) |
| 4 | stop backgrounds (dat 0/1→1, 2, 3) |
| 5 | load enemy sprite banks: dat,dat2,dat3,dat4 → `newsh{shapeFile[n-1]}.shp` into 4 slots (0 = free) |
| 6 | spawn ground enemy (ofs 25) |
| 7 | spawn top enemy (ofs 50) |
| 8 / 9 | starfield off / on |
| 10 | spawn ground2 enemy (ofs 75) |
| 11 | end level (dat==1 or all players gone → immediate) |
| 12 | 4×4 (2×2 sprites of 24×28) ground enemy, ids dat..dat+3; dat6 selects group (0/1→25, 2→0, 3→50, 4→75) |
| 13 / 14 | random enemies off / on |
| 15 | spawn sky enemy (ofs 0) |
| 16 | window text `outputs[dat-1]` (1..9) + voice sample |
| 17 | ground enemy at bottom (ey=190+dat5) |
| 18 | sky enemy at bottom (ey=190+dat5) |
| 19 | global move: exc=dat, eyc=dat2 (−99 skip), fixedmovey=dat6, enemycycle=dat5; dat3 selects group (0 link,1..3 groups,99 all) |
| 20 | global accel: excc=dat, eycc=dat2, cycle/ani from dat5/dat6 (link) |
| 21 / 22 | layer-3 over sprites on / off (background3over=1/0) |
| 23 | top enemy at bottom (ey=180+dat5) |
| 24 | global animate (link): ani=dat, cycle=dat2, mode dat3 |
| 25 | set armor = dat (link; galaga mode scaled) |
| 26 | smallEnemyAdjust = dat |
| 27 | global reverse: exrev=dat, eyrev=dat2, filter=dat3 (<17) |
| 28 / 29 | topEnemyOver false / true |
| 30 | set scroll speeds (like 2, explodeMove=dat2) |
| 31 | enemy fire override: freq[0..2]=dat,dat2,dat3, launchfreq=dat5 (link, 99=all) |
| 32 | spawn top enemy at y=190 |
| 33 | set `enemydie` (spawn-on-death enemy id) = dat for link dat4 (special-case 512/513/533/534) |
| 34 | start music fade |
| 35 | play song dat-1 |
| 36 | readyToEndLevel |
| 37 | levelEnemyFrequency = dat |
| 38 | jump curLoc to dat (rewinds eventLoc) |
| 39 | change linknum dat → dat2 |
| 40 | enemyContinualDamage = true |
| 41 | clear enemies (dat 0 = all, else slots 0–24) |
| 42 | background3over = 2 |
| 43 | background2over = dat |
| 44 | palette filter: active=dat>0, fade=dat==2, filter=dat2, brightness=dat3, newFilter=dat4, brightChg=dat5, start=dat6==0 |
| 45 | like 33 but only in arcade/2-player |
| 46 | difficulty += dat (dat2==0 or arcade), damageRate=dat3 if ≠0 |
| 47 | set armor = dat (link) |
| 48 | layer 2 not transparent |
| 49–52 | custom enemy: enemyDat[0] with sprite=dat, armor=dat6, bank=dat3; group 49→25, 50→0, 51→50, 52→75 |
| 53 | forceEvents = (dat ≠ 99) |
| 54 | event jump to dat (`JE_eventJump`; 65535 = return) |
| 55 | set xaccel=dat, yaccel=dat2 (link) |
| 56 | ground2 enemy at y=190 |
| 57 | superEnemy254Jump = dat |
| 58, 59 | not handled (warning) |
| 83+ | not handled (`default:` logs a warning). Occur in data: 1.11 (83-99), 4.13 (108), 4.18 (99) |
| 60 | mark special enemy: flagnum=dat, setto=(dat2==1) (link) |
| 61 | if globalFlags[dat-1]==dat2 skip dat3 events |
| 62 | play sound effect dat |
| 63 | skip dat events if not 2-player/arcade |
| 64 | smoothie (screen effect) dat on=dat2, data=dat3 |
| 65 | background3x1 = (dat==0) |
| 66 | skip dat2 events if initialDifficulty ≤ dat |
| 67 | level timer: on=(dat==1), jump=dat2, seconds=dat3 |
| 68 | random explosions on=(dat==1) |
| 69 | players invulnerable for dat ticks |
| 70 | jump to dat if linked enemies dat2/dat3/dat4 (or 1–19) all gone |
| 71 | jump to dat if map position ≤ dat2 |
| 72 | background3x1b = (dat==1) |
| 73 | skyEnemyOverAll = (dat==1) |
| 74 | bounce bounds xmax=dat, ymax=dat2, xmin=dat5, ymin=dat6 (link) |
| 75 | pick random live enemy with link in [dat,dat2] into newPL[dat3-80]; else skip dat4 |
| 76 | returnActive = true |
| 77 | set map Y position layer1=dat/2, layer2=dat2/2 (or dat) |
| 78 | galagaShotFreq++ (max 10) |
| 79 | boss health bars link dat, dat2 |
| 80 | skip dat events if 2-player |
| 81 | layer-2 wrap: from dat/2 to dat2/2 |
| 82 | give special weapon dat |

### 1.7 Enemy definitions
Enemy definitions are **not** in the level; levels reference enemy ids into the global `enemyDat[0..850]`
(§4, from tyrian.hdt or tyrian4.lvl). Sprite bank: `enemyDat.shapebank` (or event 49–52 override) is matched
against the 4 banks loaded by event 5; bank 21 = spriteSheet11 (coins/gems), 26 = spriteSheet10
(`tyrian2.c:JE_makeEnemy`). Bank number n → file `newsh` + `lvlmast.c:shapeFile[n-1]`:
```
 1..10: 2 4 7 8 A B C D E F    11..20: G H I J K L M N O P
21..30: Q R S T U 5 # V 0 @    31..34: 3 ^ 5 9      (comment: [25] should be '&')
```
Surprise: `newshq.shp`, `newsh@.shp`, `newsh&.shp` do not exist in the data (Q=21, @=30 are special-cased / unused);
existing: `newsh#,0-9,^,a-p,r-v,~`. `newsh6` = explosions (`JE_main`), `newsh1` = shop/mouse pointer, `newsh~` = Destruct.

---

## 2. Episode scripts `levels1..4.dat` — `tyrian2.c:JE_loadMap`, `readEpisodeString`
Whole file is a sequence of **encrypted Pascal strings** (§6.1 cipher). Sizes: 9359 / 5534 / 6052 / 11103 (levels1 = 471 strings).
- Sections start with lines beginning `*` (e.g. `**2 SECTION - LEVEL #1 ...`). To enter section `mainLevel`, the engine reads lines until it has seen `mainLevel` lines starting with `*`, then executes subsequent lines.
- Command lines start with `]X` (in files written as `]X[ args`); other lines are ignored (or consumed as text by W/Q/I). Arguments are parsed by **fixed column** with `atoi(s+N)`:

| cmd | args (column) | action |
|---|---|---|
| `]A` | — | play `tyrend.anm` |
| `]G` | origin s+4, count s+7, then pairs at s+1+8i / s+4+8i | next-level choices (planet, section) |
| `]?` | count s+4, ids at s+3+4i | set datacube list |
| `]!` | s+4 | cubeMax = n |
| `]+` | s+4 | cubeMax += n |
| `]g` | — | galaga mode |
| `]x` | — | extra/bonus game |
| `]e` | — | ENGAGE mode (forced ship/weapons) |
| `]J` | s+3 | jump to section |
| `]2` | s+3 | jump if 2-player/arcade |
| `]w` | s+3 | jump if ship 13 (Stalker) |
| `]t` | s+3 | jump if level timer expired |
| `]l` | s+3 | jump if a player died |
| `]s` | — | savepoint = current section |
| `]b` | — | autosave |
| `]i` | s+3 | shop song = n-1 |
| `]I` | next 9 lines, numbers from column 8 | item shop availability (Ship, WeapF, WeapR, Power, Engine, Opt1, Opt2, Armor, Shield) then run shop |
| `]L` | nextLevel s+9, name s+13 (9 chars), song s+22, lvlFileNum s+25, `$` at s+27/s+28 = bonus | **play level** (`]L[ 9999 004 TYRIAN   18 09` → next 4, "TYRIAN", song 18, lvl entry pair 9) |
| `]@` | — | toggle text color bank |
| `]Q` | — | end of episode; shows hint blocks delimited by `#` |
| `]P` | s+3 | n=0 → tshp2.pcx; n≤900 → tyrian.pic #n; n>900 → clear + palette `pcxpal[n-901]` |
| `]U` `]V` `]R` | s+3 | pan up / slide up / pan right to tyrian.pic #n |
| `]C` | — | fade, clear, palette 7 |
| `]B` | — | fade to black |
| `]F` | — | white flash + clear |
| `]W` | `s[2]=='y'` warning, s+4 (tens=red, units=speed) | text lines until a line starting `#` |
| `]H` | s+4 | jump if difficulty < hard |
| `]h` | — | skip next line if difficulty > normal |
| `]S` | — | net sync only |
| `]n` | — | end of scene |
| `]M` | s+3 | play song n-1 |

The loop stops when `]L` (load level) or a jump occurs.

---

## 3. Graphics

### 3.1 Sprite table format ("Sprite", used by tyrian.shp tables 0–6 and estsc.shp) — `sprite.c:load_sprites`, `blit_sprite`
```
u16 count (≤151)
repeat count: u8 populated; if populated: u16 width, u16 height, u16 size, u8 data[size]
```
Data stream (decode with `x=0`, row width = `width`):
| byte | meaning |
|---|---|
| 255, n | skip n transparent pixels |
| 254 | end of row (skip to x=width) |
| 253 | 1 transparent pixel |
| other | opaque pixel color |
Whenever x ≥ width, wrap to next row. (Colors 253–255 thus can't be drawn.)

### 3.2 Compressed "Sprite2" sheets (newsh*.shp, tyrian.shp tables 7–11) — `sprite.c:JE_loadCompShapes(B)`, `blit_sprite2`
Whole file/segment loaded raw. Header = `u16 offset[]` (LE), 1-based index: sprite *i* data at `offset[i-1]`
(relative to sheet start). Every sheet observed has first offset 608 → **304 entries**. Sprites are **12 px wide, ≤14 rows**. Stream:
```
loop: b = next byte; if b == 0x0F: end
      x += b & 0x0F (transparent skip)
      n = b >> 4; if n == 0: next row (x back to 0)  else: n opaque pixel bytes follow
```
Large objects: `blit_sprite2x2` draws idx, idx+1 (x+12), idx+19 (y+14), idx+20 → 24×28 (sheet logically 19 columns wide).

### 3.3 `tyrian.shp` (443871 B; `tyrianc.shp` 444862 = xmas) — `sprite.c:JE_loadMainShapeTables`
Offset table with count = **12** (count 11 → v1.x data, 13 → Tyrian 2000, `opentyr.c:main`).
| # | offset | size | format | content |
|---|---|---|---|---|
| 0 | 50 | 6895 | Sprite table, 60 | FONT_SHAPES |
| 1 | 6945 | 4905 | Sprite, 85 | SMALL_FONT |
| 2 | 11850 | 3346 | Sprite, 127 | TINY_FONT |
| 3 | 15196 | 116239 | Sprite, 151 | PLANET_SHAPES |
| 4 | | 27632 | Sprite, 12 (65×53/55) | FACE_SHAPES (datacube faces) |
| 5 | | 123419 | Sprite, 45 (44 populated) | OPTION_SHAPES (+help) |
| 6 | | 31109 | Sprite, 22 | WEAPON_SHAPES |
| 7 | | 30667 | Sprite2 | spriteSheet8 player shots |
| 8 | | 32403 | Sprite2 | spriteSheet9 player ships |
| 9 | | 25710 | Sprite2 | spriteSheet10 power-ups / 2-player |
| 10 | | 28503 | Sprite2 | spriteSheet11 coins, datacubes |
| 11 | | 12993 | Sprite2 | spriteSheet12 more shots |
Note: the loader does not seek for 7–11, relying on contiguity (true in data). `estsc.shp` (115338 B) = one Sprite table of 9 ending pictures (EXTRA_SHAPES, `mainint.c:JE_playCredits`). `user1/2.shp`, `newsh$.shp` = ship editor (`editship.c`, not analysed).

### 3.4 `tyrian.pic` (365969 B) — `picload.c:JE_loadPic`
Offset table count = 13 (`PCX_NUM`), first at 54. Each entry is a **headerless PCX RLE stream** for 320×200:
`b ≥ 0xC0 → run of (b & 0x3F) copies of next byte; else literal b`, until 64000 pixels (verified for all 13; each ends with 1 unused byte). No palette inside: palette = `palettes[pcxpal[id-1]]`, `pcxmast.c:pcxpal = {0,7,5,8,10,5,18,19,19,20,21,22,5}`.
Uses: 1 = shop/item screen (`game_menu.c`), 2 = menu background (title menus), 3 / 6 = in-game HUD 1P / 2P (`JE_main`), 11 = Destruct; others via `]P/]U/]V/]R`. Title logo itself: not checked (probably 4 in `opentyr.c`/`mainint.c` — unsure).

### 3.5 PCX files — `pcxload.c:JE_loadPCX`
Only `tshp2.pcx` (62168 B) is loaded (`]P[ 0`): skips 128-byte PCX header, RLE body as above, palette = **last 768 bytes as 8-bit RGB** (not 6-bit). `tyrset.pcx`, `net*.pcx`, `shipedit.pcx` are for DOS setup/net tools, unused by OpenTyrian.

---

## 4. `tyrian.hdt` items (153657 B) — `episodes.c:JE_loadItemDat`
`u32 itemPos` at offset 0 (= 16465); bytes 4..itemPos-1 = encrypted text (§6). At itemPos: `u16 counts[7]` = {780, 42, 6, 13, 30, 10, 850}
(WEAP, PORT, POWER, SHIP, OPTION, SHIELD, ENEMY; asserted). Each table has **count+1** records (index 0 included). Specials (47 = SPECIAL_NUM+1) have no count in the header. Order and record sizes (no encryption on item data):

| table | records | size | total |
|---|---|---|---|
| weapons | 781 | 80 | 62480 |
| weaponPort | 43 | 82 | 3526 |
| special | 47 | 37 | 1739 |
| powerSys (generators) | 7 | 37 | 259 |
| ships | 14 | 41 | 574 |
| options (sidekicks) | 31 | 86 | 2666 |
| shields | 11 | 37 | 407 |
| enemyDat | 851 | 77 | 65527 |
14 + all = 137192 = file size − itemPos exactly; same block in tyrian4.lvl (`lvlPos[40]`, ends at EOF).

Names: `u8 len; char name[30]` (31 bytes, len clipped to 30).

**weapon (80)**: u16 drain; u8 shotrepeat; u8 multi; u16 weapani; u8 max; u8 tx; u8 ty; u8 aim; u8 attack[8]; u8 del[8]; s8 sx[8]; s8 sy[8]; s8 bx[8]; s8 by[8]; u16 sg[8]; s8 acceleration; s8 accelerationx; u8 circlesize; u8 sound; u8 trail; u8 shipblastfilter.
**weaponPort (82)**: name(31); u8 opnum; u16 op[0][11]; u16 op[1][11]; u16 cost; u16 itemgraphic; u16 poweruse.
**special (37)**: name; u16 itemgraphic; u8 pwr; u8 stype; u16 wpn.
**powerSys (37)**: name; u16 itemgraphic; u8 power; s8 speed; u16 cost.
**ship (41)**: name; u16 shipgraphic; u16 itemgraphic; u8 ani; s8 spd; u8 dmg; u16 cost; u8 bigshipgraphic.
**option (86)**: name; u8 pwr; u16 itemgraphic; u16 cost; u8 tr; u8 option; s8 opspd; u8 ani; u16 gr[20]; u8 wport; u16 wpnum; u8 ammo; u8 stop(bool); u8 icongr.
**shield (37)**: name; u8 tpwr; u8 mpwr; u16 itemgraphic; u16 cost.
**enemyDat (77)**: u8 ani; u8 tur[3]; u8 freq[3]; s8 xmove; s8 ymove; s8 xaccel; s8 yaccel; s8 xcaccel; s8 ycaccel; s16 startx; s16 starty; s8 startxc; s8 startyc; u8 armor; u8 esize; u16 egraphic[20]; u8 explosiontype (bit0=0 → ground; >>1 = explosion no.); u8 animate; u8 shapebank; s8 xrev; s8 yrev; u16 dgr; s8 dlevel; s8 dani; u8 elaunchfreq; u16 elaunchtype (%1000 type, /1000 special); s16 value; u16 eenemydie.

---

## 5. Palettes `palette.dat` — `palette.c:loadPals`
17664 B = 23 × 768; each = 256 × (R,G,B) **6-bit** (max 63 verified). Engine expands `c8 = (c<<2)|(c>>4)`. Which palette:
- in-game: palette 5 (via pic 3/6, `pcxpal[2]=pcxpal[5]=5`); `]C` → palette 7; credits `palettes[5]` (`mainint.c:JE_playCredits`); title/menus via pic 2 → palette 7;
- shop pic 1 → palette 0; datacube faces patch colors 1..207 from `palettes[facepal[face]]`, `pcxmast.c:facepal = {1,2,3,4,6,9,11,12,16,13,14,15}`;
- levels have **no** per-level palette field; tint changes come only from event 44 filters.

## 6. Text

### 6.1 Cipher — `helptext.c:decrypt_string`, `readEncryptedString`
String = `u8 len; u8 buf[len]`. Key = `{204,129,63,255,71,19,25,62,1,99}`. Decrypt from the end:
```
for i = len-1 downto 0: buf[i] ^= key[i % 10]; if i > 0: buf[i] ^= buf[i-1]   (buf[i-1] still encrypted)
```
Used by levels*.dat, tyrian.hdt text, tyrian.cdt, cubetxt*.dat.

### 6.2 tyrian.hdt text — `helptext.c:JE_loadHelpText`
From offset 4: 468 encrypted strings (to 16465). Sections each = 1 header string (`** Section N - ... `) + N entries + 1 blank terminator, in order: helpTxt 39, planet names 21, miscText 68, miscTextB 5, key names (`menuInt_entries[6]` = 11 into menuInt[6]), menuText 7, outputs 9 (event-16 window text), topicName 6, mainMenuHelp 34, menus 1/2/3, inGameText 6, detail 6, gameSpeed 5, episode names 6, difficulty 7, gameplay 5, menu 10, inputDevices 3, networkText 4, menu 11, difficultyNameB 11, menus 12/13, joyButtons 5, superShips 11, specialName 9, destructHelp 25, weaponNames 17, destructModes, shipInfo 13×2, menu 14. (Counts from the arrays in helptext.c; I did not re-verify each section boundary, only total consumption.)

### 6.3 tyrian.cdt (1125 B) — `mainint.c:JE_playCredits`: 131 encrypted strings `credstr[]`; first char is a color/style code (e.g. `M`,`P`,`E`,`F`, `.` = blank).
### 6.4 Data cubes `cubetxt1..4.dat` (36169/8108/12294/62424 B) — `game_menu.c:load_cube`
Encrypted strings. Cube *n* begins at the n-th line starting with `*`: `*NN FF` (face sprite = atoi(buf+4)-1 into FACE_SHAPES), then title line, header line, then body lines until next `*`; empty line = paragraph break; `~word~` toggles highlight.

## 7. Audio

### 7.1 `music.mus` (153482 B) — `loudness.c:load_song`, `lds_play.c:lds_load`
Offset table, count = 41 (MUSIC_NUM); first song at 166. Each song = **LOUDNESS (LDS) module** for OPL2 (AdLib):
```
u8 mode (≤2); u16 speed; u8 tempo; u8 pattlen; u8 chandelay[9]; u8 regbd;
u16 numpatch; numpatch × 46-byte patch:
   mod_misc, mod_vol, mod_ad, mod_sr, mod_wave, car_misc, car_vol, car_ad, car_sr, car_wave,
   feedback, keyoff, portamento, glide, finetune, vibrato, vibdelay, mod_trem, car_trem,
   tremwait, arpeggio (u8 each, 21), arp_tab[12], u16 start, u16 size, u8 fms, u16 transp,
   midinst, midvelo, midkey, midtrans, middum1, middum2 (u8)
u16 numposi; numposi × 9 channels × {u16 patnum (byte offset, /2 = word index), u8 transpose}
u16 (# digital sounds, ignored)
u16 patterns[] until end of song (asserted to consume exactly)
```
Song 0 header: mode 2, tempo 7, pattlen 64, 13 patches. Play song via `play_song(n-1)` from `]M`, `]L` song, event 35.

### 7.2 `tyrian.snd` (264512 B) / `voices.snd` (132767 B; `voicesc.snd` 188275 xmas) — `nortsong.c:loadSounds`, `loadSndFile`
Offset table; tyrian.snd count = 29 (SFX), voices = 9. Payload = raw **signed 8-bit mono PCM, 11025 Hz** (`SDL_BuildAudioCVT(AUDIO_S8,1,11025,…)`), no header. For voices the engine drops the **last 100 bytes** of each sample (bad data). Sound ids 1–29 SFX, 30–38 voices (`sndmast.h` enum). SFX sizes: 6134, 6813, 1676, 5701, 9789, 9886, 10261, 6743, 5277, 12333, 14165, 12544, 7061, 27796, 4353, 7745, 8421, 5500, 14660, 9856, 24975, 16000, 3145, 1745, 3334, 11006, 5460, 3014, 9001.

## Verification (Python, all exact)
- Level header size and 22368-byte map block match `lvlPos` deltas for every level of all 4 episodes.
- Item block 137192 B ends exactly at EOF in both tyrian.hdt and tyrian4.lvl.
- tyrian.shp tables 0–6 each end exactly at the next offset; all 13 pics decode to 64000 px.
- shapes*.dat: 600 records + 520 unused bytes each.

## Uncertain / not verified
- Meaning of `char_mapFile` (ignored by engine); `mapX2/mapX3` exact role in horizontal scroll.
- Event 12 / 49–52 semantics are paraphrased; event ids 58/59 absent.
- Whether every Sprite2 sheet really has 304 meaningful entries (some offsets may be dummies).
- `tyrend.anm` (ending animation, `animlib.c`) and `demo.*` not documented.
