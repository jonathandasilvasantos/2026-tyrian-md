/* shop.c — upgrade shop economy.  The PC sells the owned item back at full
 * price when a category is opened (cash = cash * 2 - JE_cashLeft) and
 * charges the new item plus its power upgrades on leaving. */
#include "shop.h"
#include "player.h"
#include "gen_data/items.h"

uint32_t shop_cost(uint8_t cat, uint16_t item, uint8_t power)
{
    uint32_t cost = 0, step = 0;
    uint8_t i;
    switch (cat) {
    case SHOP_SHIP: cost = item <= SHIP_NUM ? ship_cost[item] : 0; break;
    case SHOP_FRONT:
    case SHOP_REAR:
        if (item == 0 || item > PORT_NUM) return 0;
        cost = port_cost[item];
        /* JE_cashLeft: each power level i adds cost * (1 + ... + i) */
        for (i = 1; i < power; i++) {
            step += (uint32_t)port_cost[item] * i;
            cost += step;
        }
        break;
    case SHOP_GENERATOR: cost = item <= POWER_NUM ? power_cost[item] : 0; break;
    case SHOP_SHIELD: cost = item <= SHIELD_NUM ? shield_cost[item] : 0; break;
    case SHOP_LEFT_OPTION: case SHOP_RIGHT_OPTION: cost = item <= OPTION_NUM ? option_cost[item] : 0; break;
    }
    return cost;
}

uint16_t shop_owned(uint8_t cat)
{
    const Player *p = &players[0];
    switch (cat) {
    case SHOP_SHIP: return p->ship;
    case SHOP_FRONT: return p->weapon_id[FRONT_WEAPON];
    case SHOP_REAR: return p->weapon_id[REAR_WEAPON];
    case SHOP_GENERATOR: return p->generator;
    case SHOP_SHIELD: return p->shield_item;
    case SHOP_LEFT_OPTION: return p->option_id[0];
    case SHOP_RIGHT_OPTION: return p->option_id[1];
    }
    return 0;
}

uint8_t shop_owned_power(uint8_t cat)
{
    const Player *p = &players[0];
    if (cat == SHOP_FRONT) return p->weapon_power[FRONT_WEAPON];
    if (cat == SHOP_REAR) return p->weapon_power[REAR_WEAPON];
    return 1;
}

uint32_t shop_budget(uint8_t cat)
{
    return players[0].cash + shop_cost(cat, shop_owned(cat), shop_owned_power(cat));
}

uint8_t shop_buy(uint8_t cat, uint16_t item, uint8_t power)
{
    Player *p = &players[0];
    uint32_t budget, cost;
    switch (cat) {
    case SHOP_SHIP: if (item > SHIP_NUM) return 0; break;
    case SHOP_FRONT: case SHOP_REAR: if (item > PORT_NUM || power < 1 || power > 11) return 0; break;
    case SHOP_GENERATOR: if (item > POWER_NUM) return 0; break;
    case SHOP_SHIELD: if (item > SHIELD_NUM) return 0; break;
    case SHOP_LEFT_OPTION: case SHOP_RIGHT_OPTION: if (item > OPTION_NUM) return 0; break;
    default: return 0;
    }
    budget = shop_budget(cat);
    cost = shop_cost(cat, item, power);
    if (cost > budget) return 0;
    p->cash = budget - cost;
    switch (cat) {
    case SHOP_SHIP: p->ship = (uint8_t)item; break;
    case SHOP_FRONT:
    case SHOP_REAR: {
        uint8_t port = cat == SHOP_FRONT ? FRONT_WEAPON : REAR_WEAPON;
        p->weapon_id[port] = item;
        p->weapon_power[port] = power;
        p->shot_multi_pos[port] = 0;
        if (port == REAR_WEAPON) p->weapon_mode = 1;
        break;
    }
    case SHOP_GENERATOR: p->generator = (uint8_t)item; break;
    case SHOP_SHIELD: p->shield_item = (uint8_t)item; break;
    case SHOP_LEFT_OPTION: p->option_id[0] = (uint8_t)item; break;
    case SHOP_RIGHT_OPTION: p->option_id[1] = (uint8_t)item; break;
    }
    return 1;
}
