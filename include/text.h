#ifndef KOVIL_TEXT_H
#define KOVIL_TEXT_H

#include "framebuffer.h"

/* Hand-drawn 5x7 pixel font. Supports A-Z 0-9 space - / ! . :
 * (lowercase input is drawn as uppercase). */

/* width in pixels of a string at a given scale (for centring) */
int text_width(const char *s, int scale);

void text_draw(Framebuffer *fb, int x, int y, int scale,
               uint32_t color, const char *s);

/* same, with a dark drop shadow for readability over the world */
void text_draw_shadow(Framebuffer *fb, int x, int y, int scale,
                      uint32_t color, const char *s);

#endif
