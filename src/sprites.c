/* sprites.c — builds the VDP sprite table every frame (no SGDK sprite
 * engine: it costs far more CPU than this direct list).
 *
 * Positions are interpolated between the previous and the current game
 * tick: the game ticks at 35 Hz but sprites move every 60 Hz frame
 * (agents.md §8).  The full list is rebuilt only on frames without a game
 * tick; on tick frames the previous list is advanced by each sprite's
 * per-frame velocity (cheap), so the logic and the sprite build never land
 * on the same frame.  Objects that appear, vanish or change frame show up
 * one frame late (two or three in busy scenes, see SPR_BUSY).  Sprite graphics (12x14 Sprite2 pieces in 2x2-cell
 * sprites) go through a VRAM cache: a piece is uploaded on first use and
 * its slot is recycled when nothing showed it this frame.  Draw order =
 * priority when the 80-sprite limit is hit: ship, enemy shots, enemies,
 * player shots, explosions; the start inside each list rotates every frame
 * so that what gets dropped changes (flicker instead of vanishing).
 */
#include "sprites.h"
#include "game/game.h"
#include "game/player.h"
#include "game/shots.h"
#include "game/enemy.h"
#include "game/special.h"
#include "game/stars.h"
#include "game/particles.h"
#include "bg.h"
#include "gen_data/sprites.h"
#include "gen_data/items.h"
#include "gen_data/shot_shapes.h"
#include "dmaq.h"

#define SPR_MAX 80
#define SPR_BUSY 24               /* sprites from which the list is rebuilt every other frame */
#define GFX_SLOTS gfx_slots /* x 4 tiles; fits this level */
static u16 gfx_slots;
#define BIG_SLOTS SPRITES_BIG_SLOTS /* x 12 tiles: composed 2x2 enemies */
/* PC playfield (24..287 x 0..183) -> screen 8..271 x 24..207; the rest is
 * the HUD frame (window plane).  bg.c uses the same offsets. */
#define SX_OFF (-16)
#define SY_OFF 24

static u16 ship_tile, gfx_tile0;
static u16 ship_loaded;          /* (ship << 8 | bank) uploaded, 0xFFFF none */
static u16 slot_gfx[SPRITES_PIECE_MAX];
static u16 slot_stamp[SPRITES_PIECE_MAX];
static u8 gfx_slot[SPR_GFX_COUNT];   /* slot + 1, 0 = not resident */
static u16 stamp, victim;
static bool piece_full;
static const u16 *const *big_bank;   /* this level's bank -> big frame tables */
static u16 big_tile0, big_victim;
static u16 big_slot_gfx[BIG_SLOTS], big_slot_stamp[BIG_SLOTS];
static u8 big_slot[BIG_GFX_COUNT];   /* slot + 1 */
static u16 rot_star, rot_particle;
static u16 rot_es, rot_e, rot_ps, rot_x;   /* rotating start of each list */
static u16 n;                    /* sprites used this frame */
static s16 pos_x4[SPR_MAX], pos_y4[SPR_MAX];   /* 12.4 VDP positions (screen + 0x80) */
static s16 vel_x4[SPR_MAX], vel_y4[SPR_MAX];   /* per-frame velocity, 12.4 */
static s16 frame_velocity;     /* 149/256 NTSC, 179/256 PAL ticks/frame */
static s16 cur_vx, cur_vy;       /* per-frame velocity (12.4) of the object being put */
/* tick delta d (-32..31) -> per-frame velocity in 1/16 px: (d * frame_velocity) >> 4 */
static s16 vel_tab[64];

/* set the velocity of the next sprites from the object's tick delta */
static inline void object_delta(s16 dx, s16 dy)
{
    cur_vx = (u16)(dx + 32) < 64 ? vel_tab[dx + 32] : (s16)((dx * frame_velocity) >> 4);
    cur_vy = (u16)(dy + 32) < 64 ? vel_tab[dy + 32] : (s16)((dy * frame_velocity) >> 4);
}
static VDPSprite *sp;            /* next free entry of vdpSpriteCache */
static s16 frac_q;               /* frac * 21: lerp factor, 1/256 units */
/* prev + (cur - prev) * frac / 12 (as (d * frac * 21) >> 8): a table row
 * per frac for |d| < 32 saves a muls.w per coordinate */
static s8 lerp_tab[TICK_DEN][64];
static const s8 *lerp_row;


void sprites_init(u16 first_tile)
{
    u16 i;
    rot_star = STAR_COUNT - 1;
    frame_velocity = IS_PAL_SYSTEM ? 179 : 149;
    gfx_slots = SPRITES_PIECE_COUNT(level_bgs[game.level].pool_need);
    big_bank = big_gfx_level[game.level];
    ship_tile = first_tile;
    gfx_tile0 = first_tile + 12;
    big_tile0 = gfx_tile0 + GFX_SLOTS * 4;
    for (i = 0; i < BIG_SLOTS; i++) {
        big_slot_gfx[i] = 0xFFFF;
        big_slot_stamp[i] = 0;
    }
    memset(big_slot, 0, sizeof big_slot);
    big_victim = 0;
    ship_loaded = 0xFFFF;
    for (i = 0; i < GFX_SLOTS; i++) {
        slot_gfx[i] = 0xFFFF;
        slot_stamp[i] = 0;
    }
    memset(gfx_slot, 0, sizeof gfx_slot);
    {
        s16 f, d;
        for (f = 0; f < TICK_DEN; f++)
            for (d = -32; d < 32; d++) lerp_tab[f][d + 32] = (s8)(((s32)d * (f * 21)) >> 8);
        for (d = -32; d < 32; d++) vel_tab[d + 32] = (s16)((d * frame_velocity) >> 4);
    }
    stamp = 1;
    victim = rot_es = rot_e = rot_ps = rot_x = rot_particle = 0;
    PAL_setColors(32, spr_palettes, 32, DMA);
    VDP_resetSprites();
}

/* Slow path of gfx_vram: upload into a slot not used this frame; 0 when
 * every slot is taken (the piece is skipped). */
static u16 gfx_upload(u16 gfx)
{
    u16 s, k;
    if (piece_full) return 0;
    for (k = 0; k < GFX_SLOTS; k++) {
        s = victim;
        if (++victim == GFX_SLOTS) victim = 0;
        if (slot_stamp[s] != stamp) {
            if (slot_gfx[s] != 0xFFFF) gfx_slot[slot_gfx[s]] = 0;
            slot_gfx[s] = gfx;
            slot_stamp[s] = stamp;
            gfx_slot[gfx] = s + 1;
            dmaq_vram(spr_tiles + gfx * 32, (gfx_tile0 + s * 4) * 32, 64);
            return gfx_tile0 + s * 4;
        }
    }
    /* Slot stamps only become used during a rebuild; a full cache stays
     * full until the next stamp. Avoid rescanning it for every spark/star. */
    piece_full = TRUE;
    return 0;
}


static inline s16 lerp(s16 prev, s16 cur)
{
    s16 d = cur - prev;
    if ((u16)(d + 32) < 64) return prev + lerp_row[d + 32];
    return prev + (s16)(((s32)d * frac_q) >> 8);
}

/* Remember sprite n's position and velocity for the tick-frame advance:
 * the object moves cur_dx/cur_dy per tick: 149/256 per NTSC frame,
 * 179/256 per PAL frame, kept in 1/16 px. */
static inline void track(s16 x, s16 y)
{
    pos_x4[n] = x << 4;
    pos_y4[n] = y << 4;
    vel_x4[n] = cur_vx;
    vel_y4[n] = cur_vy;
}

/* one 2x2-cell piece at PC playfield (x, y), written straight into the
 * sprite cache */
static inline void put(u16 gfx, s16 x, s16 y)
{
    u16 s, tile;
    if (gfx == GFX_NONE || n >= SPR_MAX) return;
    x += SX_OFF + 0x80;
    y += SY_OFF + 0x80;
    if ((u16)(x - 0x80 + 16) >= 336 || (u16)(y - 0x80 + 16) >= 240) return;
    s = gfx_slot[gfx];
    if (s) {
        slot_stamp[s - 1] = stamp;
        tile = gfx_tile0 + (s - 1) * 4;
    } else if (!(tile = gfx_upload(gfx))) {
        return;
    }
    sp->y = y;
    sp->size_link = (SPRITE_SIZE(2, 2) << 8) | (n + 1);
    sp->attribut = (spr_gfx_pal[gfx] << 13) | tile;
    sp->x = x;
    track(x, y);
    sp++;
    n++;
}

/* Four 8x8 textures share one standard cache piece. Same screen clipping
 * and motion tracking as put(), with a palette fixed to the actor line. */
static inline void put_tiny(u16 ref, s16 x, s16 y)
{
    u16 gfx = ref >> 2, slot, tile;
    if (n >= SPR_MAX) return;
    x += SX_OFF + 0x80;
    y += SY_OFF + 0x80;
    if ((u16)(x - 0x80 + 16) >= 336 || (u16)(y - 0x80 + 16) >= 240) return;
    slot = gfx_slot[gfx];
    if (slot) {
        slot_stamp[slot - 1] = stamp;
        tile = gfx_tile0 + (slot - 1) * 4;
    } else if (!(tile = gfx_upload(gfx))) return;
    sp->y = y;
    sp->size_link = (SPRITE_SIZE(1, 1) << 8) | (n + 1);
    sp->attribut = (PAL2 << 13) | (tile + (ref & 3));
    sp->x = x;
    track(x, y);
    sp++; n++;
}

/* a composed 2x2 enemy (3x4 cells) at PC playfield (x, y) = top-left */
static inline void put_big(u16 big, s16 x, s16 y)
{
    u16 s, tile = 0, k;
    if (n >= SPR_MAX) return;
    x += SX_OFF + 0x80;
    y += SY_OFF + 0x80;
    if ((u16)(x - 0x80 + 24) >= 344 || (u16)(y - 0x80 + 32) >= 256) return;
    s = big_slot[big];
    if (s) {
        big_slot_stamp[s - 1] = stamp;
        tile = big_tile0 + (s - 1) * 12;
    } else {
        for (k = 0; k < BIG_SLOTS; k++) {
            s = big_victim;
            if (++big_victim == BIG_SLOTS) big_victim = 0;
            if (big_slot_stamp[s] != stamp) {
                if (big_slot_gfx[s] != 0xFFFF) big_slot[big_slot_gfx[s]] = 0;
                big_slot_gfx[s] = big;
                big_slot_stamp[s] = stamp;
                big_slot[big] = s + 1;
                tile = big_tile0 + s * 12;
                dmaq_vram(big_tiles + big * 96, tile * 32, 192);
                break;
            }
        }
        if (!tile) return;
    }
    sp->y = y;
    sp->size_link = (SPRITE_SIZE(3, 4) << 8) | (n + 1);
    sp->attribut = (big_gfx_pal[big] << 13) | tile;
    sp->x = x;
    track(x, y);
    sp++;
    n++;
}

/* Zinglon: cached solid-white tiles, expanded to 8-pixel columns rather
 * than PC per-pixel brightening. At most 18 sprites, behind enemy bullets. */
/* Star art is preconverted. Draw last so decorative stars lose slots before
 * any gameplay object. Full-clear cell masks replace the PC pixel-zero test. */
static void draw_stars(void)
{
    s16 offset[3];
    StarMask masks[2];
    u16 i, index = rot_star, failed = 0;
    if (!starfield.initialized || (!starfield.active && !starfield.draw_astral)) return;
    bg_star_masks(masks);
    for (i = 0; i < 3; i++) {
        s16 speed = starfield.speed + (s16)i + 2;
        offset[i] = ((s32)speed * (s16)(256 - frac_q)) >> 8;
    }
    for (i = 0; i < STAR_COUNT && n < SPR_MAX; i++) {
        const Star *s = &starfield.star[index];
        u16 x, y, before;
        s16 py;
        star_coordinates(s->position, &x, &y);
        py = (s16)y - offset[s->speed - 2];
        if (!(failed & (1u << (s->color - 0x90))) && x >= 24 && x < 288 && y < 177 && py >= 0 && star_mask_clear(&masks[0], x, py) && star_mask_clear(&masks[1], x, py)) {
            object_delta(0, starfield.speed + s->speed);
            before = n;
            put_tiny(star_gfx[s->color - 0x90], x - 1, py - 1);
            if (n == before) failed |= 1u << (s->color - 0x90);
        }
        index = index ? index - 1 : STAR_COUNT - 1;
    }
    rot_star += 13;
    if (rot_star >= STAR_COUNT) rot_star -= STAR_COUNT;
}

static void draw_zinglon(void)
{
    s16 radius, left, y;
    u16 columns;
    if (special_state.zinglon <= 1) return;
    radius = (s16)special_state.zinglon - 25;
    if (radius < 0) radius = -radius;
    radius = 25 - radius;
    left = players[0].x + 7 - radius - 2;
    columns = (radius * 2 + 5 + 7) >> 3;
    cur_vx = cur_vy = 0;
    while (columns) {
        u16 width = columns > 3 ? 3 : columns;
        for (y = 0; y < 184; y += 32) {
            u16 before = n, height = y == 160 ? 3 : 4;
            put_big(beam_gfx[0], left, y);
            if (n != before) sp[-1].size_link = (SPRITE_SIZE(width, height) << 8) | n;
        }
        left += width * 8; columns -= width;
    }
}

static void draw_sidekicks(void)
{
    const Player *p = &players[0];
    u16 i;
    if (game.end_level) return;
    for (i = 0; i < 2; i++) {
        const Sidekick *s = &p->sidekick[i];
        const Option *o = &options[p->option_id[i]];
        u16 frame;
        s16 x, y;
        if (!p->option_id[i] || !o->animation) continue;
        frame = o->graphic[s->animation_frame] + s->charge;
        if (frame > 304) continue;
        x = lerp(s->px, s->x);
        y = lerp(s->py, s->y);
        object_delta(s->x - s->px, s->y - s->py);
        if (s->style == 1 || s->style == 2) put_big(option_big_gfx[frame], x - 6, y);
        else put(option_small_gfx[frame], x, y);
    }
}

static void draw_enemy_shots(void)
{
    const EnemyShot *s = &enemy_shots[rot_es], *end = &enemy_shots[ENEMY_SHOT_MAX];
    u16 k;
    if (++rot_es == ENEMY_SHOT_MAX) rot_es = 0;
    for (k = ENEMY_SHOT_MAX; k; k--, s++) {
        u16 f;
        if (s == end) s = enemy_shots;
        if (s->avail) continue;
        f = s->sgr + s->animate;
        object_delta(s->sx - s->px, s->sy - s->py);
        if (f <= 1000) put(shot_gfx[f], lerp(s->px, s->sx), lerp(s->py, s->sy));
    }
}

static void draw_enemy(const Enemy *e)
{
    const u16 *bank;
    s16 x, y, sx;
    u16 idx;
    if (!e->moved || e->enemycycle < 1 || e->enemycycle > 20 || !(bank = bank_gfx[e->shapebank])) return;
    sx = e->ex + e->mapoffset;
    /* off the playfield: before any interpolation work */
    if (sx <= -29 || sx >= 300 || e->ey <= -26 || e->ey >= 210) return;
    idx = e->egr[e->enemycycle - 1];
    if (idx > 284) return;
    x = lerp(e->px, sx);
    y = lerp(e->py, e->ey);
    object_delta(sx - e->px, e->ey - e->py);
    if (e->size == 1 && big_bank[e->shapebank] && big_bank[e->shapebank][idx] != GFX_NONE) {
        if (e->ey > -26 && e->ey < 182) put_big(big_bank[e->shapebank][idx], x - 6, y - 7);
    } else if (e->size == 1) {
        if (e->ey > -13) {
            put(bank[idx], x - 6, y - 7);
            put(bank[idx + 1], x + 6, y - 7);
        }
        if (e->ey > -26 && e->ey < 182) {
            put(bank[idx + 19], x - 6, y + 7);
            put(bank[idx + 20], x + 6, y + 7);
        }
    } else if (e->ey > -13) {
        put(bank[idx], x, y);
    }
}

/* two linear pointer walks (rot..end, 0..rot): indexing the ~100-byte
 * Enemy structs would cost a mulu each */
static void draw_enemies(void)
{
    u16 start = rot_e, i;
    const Enemy *e;
    if ((rot_e += 7) >= ENEMY_MAX) rot_e -= ENEMY_MAX;
    for (i = start, e = &enemies[start]; i < ENEMY_MAX && n < SPR_MAX; i++, e++)
        if (enemy_avail[i] != EA_FREE) draw_enemy(e);
    for (i = 0, e = enemies; i < start && n < SPR_MAX; i++, e++)
        if (enemy_avail[i] != EA_FREE) draw_enemy(e);
}

static void draw_player_shots(void)
{
    const PlayerShot *s = &player_shots[rot_ps], *end = &player_shots[PLAYER_SHOT_MAX];
    u16 k;
    if ((rot_ps += 7) >= PLAYER_SHOT_MAX) rot_ps -= PLAYER_SHOT_MAX;
    for (k = PLAYER_SHOT_MAX; k; k--, s++) {
        if (s == end) s = player_shots;
        if (!s->avail || !s->frame) continue;
        object_delta(s->x - s->px, s->y - s->py);
        if (s->frame > 60000) {
            u16 shape = s->frame - 60001, i;
            s16 x = lerp(s->px, s->x) + 1, y = lerp(s->py, s->y);
            if (shape >= OPTION_SHAPE_COUNT) continue;
            for (i = 0; i < special_counts[shape]; i++) {
                const SpecialPiece *piece = &special_pieces[special_starts[shape] + i];
                put_big(piece->gfx, x + piece->x, y + piece->y);
            }
        } else if (s->frame <= 1000) {
            put(shot_gfx[s->frame], lerp(s->px, s->x) + 1, lerp(s->py, s->y));
        }
    }
}

/* Cosmetic sparks yield cache/sprite slots to all gameplay objects. */
static void draw_particles(void)
{
    u16 i, index = rot_particle;
    if (!particles.live) return;
    rot_particle = (rot_particle + 7) & (PARTICLE_MAX - 1);
    for (i = 0; i < PARTICLE_MAX && n < SPR_MAX; i++) {
        const Particle *p = &particles.item[index];
        if (p->life && p->x >= 24 && p->x < 288 && p->y >= 0 && p->y < 184) {
            object_delta(p->dx, p->dy);
            put_tiny(particle_gfx[(p->color & 0xF0) / 2 + (p->life >> 1)],
                lerp(p->x - p->dx, p->x) - 1, lerp(p->y - p->dy, p->y) - 1);
        }
        index = (index + 1) & (PARTICLE_MAX - 1);
    }
}

static void draw_explosions(void)
{
    const Explosion *x = &explosions[rot_x], *end = &explosions[EXPLOSION_MAX];
    u16 k;
    if ((rot_x += 7) >= EXPLOSION_MAX) rot_x -= EXPLOSION_MAX;
    for (k = EXPLOSION_MAX; k; k--, x++) {
        if (x == end) x = explosions;
        if (!x->ttl || x->sprite >= 304) continue;
        object_delta(x->x - x->px, x->y - x->py);
        put(explo_gfx[x->sprite + 1], lerp(x->px, x->x), lerp(x->py, x->y));
    }
}

/* tick frame: move the previous list along its velocities */
static void advance(void)
{
    u16 i;
    VDPSprite *v = vdpSpriteCache;
    for (i = 0; i < n; i++, v++) {
        pos_x4[i] += vel_x4[i];
        pos_y4[i] += vel_y4[i];
        v->x = pos_x4[i] >> 4;
        v->y = pos_y4[i] >> 4;
    }
    VDP_updateSprites(n, DMA_QUEUE);
}

void sprites_update(u16 frac, bool ticked)
{
    const Player *p = &players[0];
    u16 key;

    /* Busy scenes (many sprites) rebuild only every other non-tick frame:
     * the full build costs up to ~70k cycles there; in between the list is
     * advanced like on tick frames (new/removed objects show a frame or
     * two late). */
    {
        static bool skipped;
        if (n && (ticked || (n >= SPR_BUSY && !skipped))) {
            if (!ticked) skipped = TRUE;
            advance();
            return;
        }
        skipped = FALSE;
    }
    if (++stamp == 0) stamp = 1;
    piece_full = FALSE;
    frac_q = frac * 21;
    lerp_row = lerp_tab[frac];
    n = 0;

    if (p->is_alive) {
        key = (u16)p->ship << 8 | (u8)(p->banking + 2);
        if (key != ship_loaded) {
            dmaq_vram(ship_tiles + ((p->ship - 1) * SHIP_BANK_FRAMES + p->banking + 2) * 96, ship_tile * 32, 192);
            ship_loaded = key;
        }
        /* invulnerability: blink (the PC blends the ship instead) */
        if (!(p->invulnerable_ticks & 2)) {
            s16 x = lerp(p->px, p->x) - 5 + SX_OFF + 0x80, y = lerp(p->py, p->y) - 7 + SY_OFF + 0x80;
            VDPSprite *v = vdpSpriteCache;
            v->y = y;
            v->size_link = (SPRITE_SIZE(3, 4) << 8) | 1;
            v->attribut = TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, ship_tile);
            v->x = x;
            object_delta(p->x - p->px, p->y - p->py);
            track(x, y);
            n = 1;
        }
    }
    sp = &vdpSpriteCache[n];
    draw_enemy_shots();
    draw_zinglon();
    draw_sidekicks();
    draw_enemies();
    draw_player_shots();
    draw_explosions();
    draw_particles();
    draw_stars();
    if (n == 0) {   /* keep one (off-screen) sprite so the list is valid */
        VDP_setSpriteFull(0, -32, -32, SPRITE_SIZE(1, 1), 0, 0);
        object_delta(0, 0);
        track(-32 + 0x80, -32 + 0x80);
        n = 1;
    }
    vdpSpriteCache[n - 1].link = 0;
    VDP_updateSprites(n, DMA_QUEUE);
}
