#ifndef KOVIL_FRAMEBUFFER_H
#define KOVIL_FRAMEBUFFER_H

#include <stdint.h>

/* We render internally at 320x200 (the classic VGA "mode 13h" resolution)
 * and let SDL upscale it with nearest-neighbour filtering. Chunky pixels
 * are the aesthetic, and 64,000 pixels is cheap enough to redraw fully
 * every frame in software. */
#define FB_WIDTH  320
#define FB_HEIGHT 200

/* Pack three 8-bit channels into one 32-bit 0xAARRGGBB word.
 * Alpha is forced opaque. This layout matches SDL_PIXELFORMAT_ARGB8888,
 * so the buffer can be handed to the GPU with zero conversion. */
#define COLOR_RGB(r, g, b) \
    (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

typedef struct {
    uint32_t *pixels; /* owned by this struct; w*h words, row-major */
    int w, h;
} Framebuffer;

/* Returns NULL on allocation failure — caller must check. */
Framebuffer *fb_create(void);
void fb_destroy(Framebuffer *fb);

void fb_clear(Framebuffer *fb, uint32_t color);
void fb_put(Framebuffer *fb, int x, int y, uint32_t color);
void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color);

/* Scale a colour's brightness: 255 = unchanged, 0 = black.
 * Used for wall-side shading and distance fade. */
uint32_t color_scale(uint32_t color, uint8_t brightness);

/* Blend a toward b: t=0 gives a, t=255 gives b. Used for fog. */
uint32_t color_lerp(uint32_t a, uint32_t b, uint8_t t);

/* Fixed 64-colour temple palette. palette_init builds a 32K lookup
 * table so snapping the whole frame to the palette is one array read
 * per pixel. Call once at startup, before palette_quantize_frame. */
void palette_init(void);
void palette_quantize_frame(Framebuffer *fb);

/* Horror stays local to the enemies: when an awakened guardian has the
 * player in sight, main() ramps this up and the screen edges bleed
 * toward dark red. 0 = no effect. */
void fb_dread(Framebuffer *fb, uint8_t intensity);

/* Full-screen tint toward a colour (damage flash, death fade). */
void fb_flash(Framebuffer *fb, uint32_t color, uint8_t amount);

/* Blit a TEX_SIZE-square sprite (alpha-0 skipped) scaled up by an
 * integer factor — used for the first-person weapon overlay. */
void fb_blit_tex(Framebuffer *fb, const uint32_t *tex, int dx, int dy,
                 int scale);

#endif
