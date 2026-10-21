/* Story campaign state; OpenTyrian mainint.c:JE_nextEpisode. */
#ifndef GAME_EPISODE_H
#define GAME_EPISODE_H
#include "types.h"
/* New game resets equipment/funds; advancing preserves both and RNG. */
void game_new(uint8_t episode, uint8_t difficulty);
void game_next_episode(void);
#endif
