#include "framebuffer.h"

#include <stdlib.h>

Framebuffer *fb_create(void)
{
    Framebuffer *fb = malloc(sizeof *fb);
    if (!fb)
        return NULL;

    fb->w = FB_WIDTH;
    fb->h = FB_HEIGHT;
    fb->pixels = malloc((size_t)fb->w * (size_t)fb->h * sizeof *fb->pixels);
    if (!fb->pixels) {
        free(fb);
        return NULL;
    }
    return fb;
}

void fb_destroy(Framebuffer *fb)
{
    if (!fb)
        return;
    free(fb->pixels);
    free(fb);
}

void fb_clear(Framebuffer *fb, uint32_t color)
{
    /* Pointer arithmetic instead of [y*w+x] indexing: one running pointer
     * walks the whole buffer. Same machine code with -O2, but it makes the
     * "a 2D image is really a flat array" idea explicit. */
    uint32_t *p = fb->pixels;
    uint32_t *end = p + fb->w * fb->h;
    while (p < end)
        *p++ = color;
}

void fb_put(Framebuffer *fb, int x, int y, uint32_t color)
{
    if (x < 0 || x >= fb->w || y < 0 || y >= fb->h)
        return;
    fb->pixels[y * fb->w + x] = color;
}

void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color)
{
    /* Clip against the buffer edges so callers never have to. */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;

    for (int row = 0; row < h; row++) {
        uint32_t *p = fb->pixels + (y + row) * fb->w + x;
        for (int col = 0; col < w; col++)
            *p++ = color;
    }
}

uint32_t color_scale(uint32_t color, uint8_t brightness)
{
    /* Unpack each channel with shifts and masks, scale it by
     * brightness/256 using integer multiply + shift (no floats,
     * no division), and repack. Runs for every wall pixel. */
    uint32_t r = ((color >> 16 & 0xFFu) * brightness) >> 8;
    uint32_t g = ((color >> 8  & 0xFFu) * brightness) >> 8;
    uint32_t b = ((color       & 0xFFu) * brightness) >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint32_t color_lerp(uint32_t a, uint32_t b, uint8_t t)
{
    /* Per-channel linear interpolation, integer only: a + (b-a)*t/256.
     * The intermediate is signed because b-a can be negative. */
    int32_t ar = a >> 16 & 0xFF, ag = a >> 8 & 0xFF, ab = a & 0xFF;
    int32_t br = b >> 16 & 0xFF, bg = b >> 8 & 0xFF, bb = b & 0xFF;
    uint32_t r = (uint32_t)(ar + ((br - ar) * t >> 8));
    uint32_t g = (uint32_t)(ag + ((bg - ag) * t >> 8));
    uint32_t bl = (uint32_t)(ab + ((bb - ab) * t >> 8));
    return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

/* ---- 64-colour palette ----------------------------------------------
 * Eight ramps of eight, all derived from temple materials. A real
 * 1994 game had a hardware palette exactly like this; we emulate it by
 * snapping every rendered pixel to its nearest palette entry, which is
 * what produces the characteristic banding in the fog. */
/* "Resurrect 64" by Kerrie Lake (lospec.com/palette-list/resurrect-64),
 * a widely used, hand-balanced 64-colour pixel-art palette. Using a
 * professionally tuned palette instead of home-made ramps is what real
 * retro games did too — the artistry was in the palette. */
static const uint8_t palette[64][3] = {
    {46,34,47},{62,53,70},{98,85,101},{150,108,108},
    {171,148,122},{105,79,98},{127,112,138},{155,171,178},
    {199,220,208},{255,255,255},{110,39,39},{179,56,49},
    {234,79,54},{245,125,74},{174,35,52},{232,59,59},
    {251,107,29},{247,150,23},{249,194,43},{122,48,69},
    {158,69,57},{205,104,61},{230,144,78},{251,185,84},
    {76,62,36},{103,102,51},{162,169,71},{213,224,75},
    {251,255,134},{22,90,76},{35,144,99},{30,188,115},
    {145,219,105},{205,223,108},{49,54,56},{55,78,74},
    {84,126,100},{146,169,132},{178,186,144},{11,94,101},
    {11,138,143},{14,175,155},{48,225,185},{143,248,226},
    {50,51,83},{72,74,119},{77,101,180},{77,155,230},
    {143,211,255},{69,41,63},{107,62,117},{144,94,169},
    {168,132,243},{234,173,237},{117,60,84},{162,75,111},
    {207,101,127},{237,128,153},{131,28,93},{195,36,84},
    {240,79,120},{246,129,129},{252,167,144},{253,203,176},
};

/* One entry per 15-bit colour (5 bits per channel = 32768 colours),
 * holding the nearest palette colour, precomputed once. 128KB buys a
 * per-pixel quantise that is a shift-and-mask plus one array read. */
static uint32_t quant_lut[1 << 15];

void palette_init(void)
{
    for (uint32_t key = 0; key < (1u << 15); key++) {
        /* expand the 5-bit channels back to 8-bit midpoints */
        uint32_t r = ((key >> 10 & 31) << 3) + 4;
        uint32_t g = ((key >> 5  & 31) << 3) + 4;
        uint32_t b = ((key       & 31) << 3) + 4;

        uint32_t best = 0, best_d = 0xFFFFFFFFu;
        for (int i = 0; i < 64; i++) {
            int32_t dr = (int32_t)r - palette[i][0];
            int32_t dg = (int32_t)g - palette[i][1];
            int32_t db = (int32_t)b - palette[i][2];
            /* weighted squared distance: eyes are most sensitive to
             * green, least to blue */
            uint32_t d = (uint32_t)(2 * dr * dr + 4 * dg * dg + db * db);
            if (d < best_d) {
                best_d = d;
                best = (uint32_t)i;
            }
        }
        quant_lut[key] = COLOR_RGB(palette[best][0], palette[best][1],
                                   palette[best][2]);
    }
}

void fb_flash(Framebuffer *fb, uint32_t color, uint8_t amount)
{
    if (amount == 0)
        return;
    uint32_t *p = fb->pixels;
    uint32_t *end = p + fb->w * fb->h;
    for (; p < end; p++)
        *p = color_lerp(*p, color, amount);
}

void fb_blit_tex(Framebuffer *fb, const uint32_t *tex, int dx, int dy,
                 int scale)
{
    for (int ty = 0; ty < 64; ty++) {
        for (int tx = 0; tx < 64; tx++) {
            uint32_t c = tex[ty * 64 + tx];
            if ((c >> 24) == 0)
                continue;
            fb_fill_rect(fb, dx + tx * scale, dy + ty * scale,
                         scale, scale, c);
        }
    }
}

void fb_dread(Framebuffer *fb, uint8_t intensity)
{
    if (intensity == 0)
        return;

    const uint32_t red = COLOR_RGB(70, 10, 8);
    const int hw = fb->w / 2, hh = fb->h / 2;

    for (int y = 0; y < fb->h; y++) {
        /* edge factor 0..255 per axis; the max of the two makes a
         * rectangular border rather than a corner-only glow */
        int ey = (y < hh ? hh - y : y - hh) * 255 / hh;
        uint32_t *p = fb->pixels + y * fb->w;
        for (int x = 0; x < fb->w; x++) {
            int ex = (x < hw ? hw - x : x - hw) * 255 / hw;
            int edge = ex > ey ? ex : ey;
            int amt = edge * intensity >> 8;
            p[x] = color_lerp(p[x], red, (uint8_t)amt);
        }
    }
}

void palette_quantize_frame(Framebuffer *fb)
{
    uint32_t *p = fb->pixels;
    uint32_t *end = p + fb->w * fb->h;
    for (; p < end; p++) {
        uint32_t c = *p;
        /* crush 8-8-8 down to the 5-5-5 lookup key */
        uint32_t key = (c >> 9 & 0x7C00u)  /* red   -> bits 14..10 */
                     | (c >> 6 & 0x03E0u)  /* green -> bits  9..5  */
                     | (c >> 3 & 0x001Fu); /* blue  -> bits  4..0  */
        *p = quant_lut[key];
    }
}
