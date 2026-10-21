/* shop.h — upgrade shop economy (OpenTyrian game_menu.c JE_cashLeft,
 * mainint.c JE_getCost / weapon_upgrade_cost): trade-in at full value. */
#ifndef GAME_SHOP_H
#define GAME_SHOP_H

#include "types.h"

/* categories = Shop rows (gen_data/script.h) */
#define SHOP_SHIP 0
#define SHOP_FRONT 1
#define SHOP_REAR 2
#define SHOP_GENERATOR 3
#define SHOP_LEFT_OPTION 5
#define SHOP_RIGHT_OPTION 6
#define SHOP_SHIELD 8

/* price of item at power (power only for weapons, 1..11) */
uint32_t shop_cost(uint8_t cat, uint16_t item, uint8_t power);
uint16_t shop_owned(uint8_t cat);
uint8_t shop_owned_power(uint8_t cat);
/* cash available for cat: cash + value of the owned item (sold back) */
uint32_t shop_budget(uint8_t cat);
/* buy (replacing the owned item); 0 if not affordable */
uint8_t shop_buy(uint8_t cat, uint16_t item, uint8_t power);

#endif
