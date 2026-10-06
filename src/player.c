#include "player.h"

#include <math.h>

/* The player is a circle of this radius (in tiles). Collision checks the
 * four corners of its bounding box so you can't clip a wall corner. */
#define PLAYER_RADIUS 0.20

/* plane length 0.66 with a unit dir vector gives a ~66 degree horizontal
 * field of view — the classic Wolfenstein feel. Bigger = wider + more
 * edge distortion. */
#define PLANE_LEN 0.66

/* Jump tuning: initial velocity and gravity in wall-heights/second.
 * v0^2 / 2g gives a peak of ~0.45 wall heights — enough to feel
 * airborne without seeing over the walls. */
#define JUMP_V0 2.3
#define GRAVITY 6.0

void player_init(Player *p, double x, double y, double angle)
{
    p->x = x;
    p->y = y;
    p->dirX = cos(angle);
    p->dirY = sin(angle);
    /* plane is dir rotated 90 degrees, scaled to the FOV length */
    p->planeX = -p->dirY * PLANE_LEN;
    p->planeY =  p->dirX * PLANE_LEN;

    p->pitch = 0.0;
    p->z = 0.0;
    p->vz = 0.0;
    p->on_ground = 1;
    p->hp = PLAYER_MAX_HP;

    /* buffs end on respawn; the inventory deliberately survives */
    p->buff_str_t = 0.0;
    p->buff_spd_t = 0.0;
    p->poison_t = 0.0;
    p->blocking = 0;
}

void player_jump(Player *p)
{
    if (p->on_ground) {
        p->vz = JUMP_V0;
        p->on_ground = 0;
    }
}

void player_physics(Player *p, double dt)
{
    if (p->on_ground)
        return;
    /* Semi-implicit Euler: update velocity first, then position.
     * More stable than the naive order at variable frame rates. */
    p->vz -= GRAVITY * dt;
    p->z += p->vz * dt;
    if (p->z <= 0.0) {
        p->z = 0.0;
        p->vz = 0.0;
        p->on_ground = 1;
    }
}

void player_rotate(Player *p, double angle)
{
    /* Standard 2D rotation matrix applied to both vectors. Rotating them
     * together keeps the camera plane locked perpendicular to the view. */
    double c = cos(angle), s = sin(angle);
    double old;

    old = p->dirX;
    p->dirX = p->dirX * c - p->dirY * s;
    p->dirY = old * s + p->dirY * c;

    old = p->planeX;
    p->planeX = p->planeX * c - p->planeY * s;
    p->planeY = old * s + p->planeY * c;
}

static int blocked(const Map *m, double x, double y)
{
    /* walkable, not merely non-solid: the tank's deep water stops
     * feet even though sight and thrown flame pass over it */
    return !map_walkable(m, (int)(x - PLAYER_RADIUS), (int)(y - PLAYER_RADIUS))
        || !map_walkable(m, (int)(x + PLAYER_RADIUS), (int)(y - PLAYER_RADIUS))
        || !map_walkable(m, (int)(x - PLAYER_RADIUS), (int)(y + PLAYER_RADIUS))
        || !map_walkable(m, (int)(x + PLAYER_RADIUS), (int)(y + PLAYER_RADIUS));
}

void player_move(Player *p, const Map *m, double dx, double dy)
{
    /* Axis-separated collision: try X and Y independently. If a diagonal
     * move hits a wall on one axis only, the other axis still applies,
     * so the player slides along the wall instead of sticking to it. */
    if (!blocked(m, p->x + dx, p->y))
        p->x += dx;
    if (!blocked(m, p->x, p->y + dy))
        p->y += dy;
}
