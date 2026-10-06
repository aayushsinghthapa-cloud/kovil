#ifndef KOVIL_PARTICLES_H
#define KOVIL_PARTICLES_H

#include "framebuffer.h"
#include "player.h"

/* Fixed-pool particle system threaded on a singly linked free list:
 * spawning pops a node, expiring pushes it back — no allocation ever
 * happens per particle, and both operations are O(1). */

void particles_reset(void);
void particles_update(double dt);

/* stone chips / sparks flying from an impact point */
void particles_burst(double x, double y, double z, int count,
                     uint32_t color);
/* a slow-rising golden beam column (boss death, shrine lighting) */
void particles_beam(double x, double y, uint32_t color);

/* project and draw every particle, clipped by the wall depth buffer;
 * call between raycast_sprites and the HUD */
void particles_render(Framebuffer *fb, const Player *p);

#endif
