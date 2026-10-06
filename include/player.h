#ifndef KOVIL_PLAYER_H
#define KOVIL_PLAYER_H

#include "inventory.h"
#include "map.h"

typedef struct {
    double x, y;           /* position in map units (1.0 = one tile) */
    double dirX, dirY;     /* facing direction, unit length */
    double planeX, planeY; /* camera plane, perpendicular to dir;
                            * its length (0.66) sets the field of view */

    double pitch;          /* look up/down, in screen pixels of horizon
                            * shift (positive = looking up) */
    double z;              /* height above the floor in wall units
                            * (0 = standing, jumping raises it) */
    double vz;             /* vertical velocity for the jump arc */
    int on_ground;

    int hp;                /* 0..100 */

    Inventory inv;
    double buff_str_t;     /* strength potion seconds remaining */
    double buff_spd_t;     /* swiftness potion seconds remaining */
    double poison_t;       /* naga venom seconds remaining */
    int blocking;          /* shield raised (sneaking with one) */
} Player;

#define PLAYER_MAX_HP 100

/* Resets pose, health and buffs — but NOT the inventory, which
 * survives floor descents and death (call inventory_init separately
 * when a brand-new run begins). */
void player_init(Player *p, double x, double y, double angle);
void player_rotate(Player *p, double angle);

/* Move by a world-space delta, sliding along walls on collision. */
void player_move(Player *p, const Map *m, double dx, double dy);

/* Gravity integration for the jump arc; call once per frame. */
void player_physics(Player *p, double dt);
void player_jump(Player *p);

#endif
