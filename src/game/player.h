/* player.h — the player's ship (OpenTyrian mainint.c:JE_playerMovement). */
#ifndef GAME_PLAYER_H
#define GAME_PLAYER_H

#include "types.h"

#define FRONT_WEAPON 0
#define REAR_WEAPON 1
/* shot bays (shotRepeat / shotMultiPos indexes) */
#define SHOT_MISC 2
#define SHOT_BAYS 3

typedef struct {
    int16_t x, y;                  /* PC playfield coordinates */
    int16_t px, py;                /* position at the previous tick (render interpolation) */
    int16_t x_velocity, y_velocity;
    uint8_t x_friction_ticks, y_friction_ticks;
    int16_t last_x_shot_move, last_y_shot_move;
    int16_t delta_x_shot_move, delta_y_shot_move;
    int8_t banking;                /* -2..2, ship sprite */
    uint8_t ship;                  /* items.ship */
    uint8_t generator;
    uint16_t weapon_id[2];         /* front / rear port */
    uint8_t weapon_power[2];       /* 1..11 */
    uint8_t weapon_mode;           /* rear mode 1..opnum */
    uint8_t shot_repeat[SHOT_BAYS];
    uint8_t shot_multi_pos[SHOT_BAYS];
    uint8_t mode_button_held;
    uint8_t is_alive, exploding_ticks;
    uint8_t armor, initial_armor, shield, shield_max, shield_item;
    uint8_t invulnerable_ticks;
    uint8_t hit_area_x, hit_area_y;  /* shot_hit_area_x/y */
    uint32_t cash;
    uint16_t debug_held;           /* DEBUG_WEAPONS buttons, edge detection */
} Player;

extern Player players[1];

void player_init(void);          /* new game items */
void player_start_level(void);
void player_tick(uint16_t pad);
/* JE_playerCollide: ship against enemies and items */
void player_collide(void);
/* JE_playerDamage: returns the damage taken through shields (0 if absorbed) */
uint8_t player_damage(uint8_t dmg);
/* power_up_weapon */
uint8_t player_power_up(uint8_t port);

#endif
