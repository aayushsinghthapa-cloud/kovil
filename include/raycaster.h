#ifndef KOVIL_RAYCASTER_H
#define KOVIL_RAYCASTER_H

#include "entity.h"
#include "framebuffer.h"
#include "map.h"
#include "player.h"

void raycast_render(Framebuffer *fb, const Map *m, const Player *p);

/* Draw all entities as billboard sprites, farthest first, each column
 * clipped against the wall depth buffer. Call after raycast_render. */
void raycast_sprites(Framebuffer *fb, const Player *p,
                     const EntityList *el);

/* Global light level (1.0 = steady). main() feeds a slowly wandering
 * value here so the whole temple breathes like flame-light. */
void raycast_set_flicker(double f);

/* Seconds since start; drives the water animation in the tank. */
void raycast_set_time(double t);

/* Per-column wall distances from the last raycast_render call;
 * FB_WIDTH entries. Sprites (M3) clip against this. */
const double *raycast_depth(void);

#endif
