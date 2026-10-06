#include "texture.h"

#include <math.h>
#include <stdlib.h>

#include "framebuffer.h"
#include "util.h"

/* One flat allocation holds all textures back to back; TEX_COUNT slots
 * of TEX_SIZE*TEX_SIZE pixels. Owned by this module. */
static uint32_t *tex_data = NULL;

static uint32_t *slot(TextureId id)
{
    return tex_data + (size_t)id * TEX_SIZE * TEX_SIZE;
}

const uint32_t *texture_pixels(TextureId id)
{
    return slot(id);
}

/* ---- the Minecraft-style texel grid ---------------------------------
 * Block textures are authored on a 16x16 grid; each "texel" is drawn
 * as a 4x4 pixel square. Chunky flat-colour texels with bevel
 * highlights read clearly at any distance — noisy per-pixel detail
 * just turns to mush, which is the lesson of every blocky game. */

#define GRID 16
#define TXL (TEX_SIZE / GRID) /* 4 pixels per texel */

static void put_texel(uint32_t *px, int tx, int ty, uint32_t c)
{
    for (int y = 0; y < TXL; y++)
        for (int x = 0; x < TXL; x++)
            px[(ty * TXL + y) * TEX_SIZE + (tx * TXL + x)] = c;
}

/* deterministic per-texel hash, 0..255: variation without noise mush */
static int hash8(int x, int y, uint32_t salt)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u
                 + salt;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (int)(h & 255u);
}

/* brightness-adjust a colour by a signed amount */
static uint32_t adj(uint32_t base, int amount)
{
    int k = 190 + amount;
    if (k < 0) k = 0;
    if (k > 255) k = 255;
    return color_scale(base, (uint8_t)k);
}

/* ---- block textures --------------------------------------------------- */

/* granite cut into brick courses, beveled like a Minecraft block */
static void gen_stone_brick(uint32_t *px)
{
    const uint32_t base = COLOR_RGB(178, 170, 152);
    const uint32_t mortar = COLOR_RGB(84, 76, 70);

    for (int ty = 0; ty < GRID; ty++) {
        int course = ty / 4;
        int off = (course & 1) * 4; /* half-brick offset per course */
        for (int tx = 0; tx < GRID; tx++) {
            int bx = (tx + off) & 7;
            if (ty % 4 == 0 || bx == 0) {
                put_texel(px, tx, ty, mortar);
                continue;
            }
            /* per-brick tint + bevel: light top-left, dark bottom */
            int tint = (hash8(course, (tx + off) / 8, 11u) & 31) - 16;
            if (ty % 4 == 1) tint += 26;
            if (ty % 4 == 3) tint -= 20;
            if (bx == 1) tint += 12;
            if (bx == 7) tint -= 12;
            put_texel(px, tx, ty, adj(base, tint));
        }
    }
}

/* chiseled sandstone pillar block with a carved lotus diamond */
static void gen_chiseled(uint32_t *px)
{
    const uint32_t base = COLOR_RGB(226, 176, 106);
    const uint32_t carve = COLOR_RGB(150, 104, 48);
    const uint32_t edge = COLOR_RGB(249, 210, 140);

    for (int ty = 0; ty < GRID; ty++) {
        for (int tx = 0; tx < GRID; tx++) {
            int tint = (hash8(tx, ty, 22u) & 15) - 8;
            uint32_t c = adj(base, tint);
            /* block frame */
            if (tx == 0 || ty == 0) c = edge;
            else if (tx == GRID - 1 || ty == GRID - 1) c = carve;
            else {
                /* carved diamond, hollow centre */
                int d = abs(tx - 8) + abs(ty - 8);
                if (d == 4 || d == 5) c = carve;
                else if (d == 3) c = edge;
                else if (d <= 1) c = carve;
            }
            put_texel(px, tx, ty, c);
        }
    }
}

/* red sandstone bricks with one gold inlay course — sanctum walls */
static void gen_sanctum_brick(uint32_t *px)
{
    const uint32_t base = COLOR_RGB(186, 96, 50);
    const uint32_t mortar = COLOR_RGB(92, 40, 24);
    const uint32_t gold = COLOR_RGB(249, 194, 43);
    const uint32_t gold_dk = COLOR_RGB(205, 140, 30);

    for (int ty = 0; ty < GRID; ty++) {
        /* gold inlay band across the middle */
        if (ty == 7 || ty == 8) {
            for (int tx = 0; tx < GRID; tx++) {
                uint32_t c = (ty == 7) ? gold : gold_dk;
                if ((tx & 3) == 0)
                    c = adj(c, -30); /* segmented inlay */
                put_texel(px, tx, ty, c);
            }
            continue;
        }
        int course = ty / 4;
        int off = (course & 1) * 4;
        for (int tx = 0; tx < GRID; tx++) {
            int bx = (tx + off) & 7;
            if (ty % 4 == 0 || bx == 0) {
                put_texel(px, tx, ty, mortar);
                continue;
            }
            int tint = (hash8(course, (tx + off) / 8, 33u) & 31) - 16;
            if (ty % 4 == 1) tint += 22;
            if (ty % 4 == 3) tint -= 18;
            put_texel(px, tx, ty, adj(base, tint));
        }
    }
}

/* verdigris copper door: vertical planks, brass studs */
static void gen_door_planks(uint32_t *px)
{
    const uint32_t base = COLOR_RGB(96, 148, 112);
    const uint32_t seam = COLOR_RGB(40, 78, 58);
    const uint32_t brass = COLOR_RGB(226, 176, 106);

    for (int ty = 0; ty < GRID; ty++) {
        for (int tx = 0; tx < GRID; tx++) {
            uint32_t c;
            if ((tx & 3) == 0)
                c = seam;
            else {
                int tint = (hash8(tx / 4, ty, 44u) & 23) - 12;
                if ((tx & 3) == 1) tint += 14;
                c = adj(base, tint);
            }
            /* brass studs in two columns, three rows */
            if ((tx == 2 || tx == 13) && (ty == 2 || ty == 8 || ty == 13))
                c = brass;
            put_texel(px, tx, ty, c);
        }
    }
}

/* sandstone slab floor: big 8x8 flags */
static void gen_slab_floor(uint32_t *px)
{
    const uint32_t base = COLOR_RGB(206, 152, 84);
    const uint32_t gap = COLOR_RGB(122, 82, 40);

    for (int ty = 0; ty < GRID; ty++) {
        for (int tx = 0; tx < GRID; tx++) {
            if ((tx & 7) == 0 || (ty & 7) == 0) {
                put_texel(px, tx, ty, gap);
                continue;
            }
            int tint = (hash8(tx / 8, ty / 8, 55u) & 31) - 16
                       + ((hash8(tx, ty, 56u) & 7) - 4);
            if ((ty & 7) == 1) tint += 16;
            put_texel(px, tx, ty, adj(base, tint));
        }
    }
}

/* dark stone ceiling in 4x4 coffers */
static void gen_coffer_ceiling(uint32_t *px)
{
    const uint32_t base = COLOR_RGB(120, 108, 116);
    const uint32_t line = COLOR_RGB(66, 58, 66);

    for (int ty = 0; ty < GRID; ty++) {
        for (int tx = 0; tx < GRID; tx++) {
            if ((tx & 3) == 0 || (ty & 3) == 0) {
                put_texel(px, tx, ty, line);
                continue;
            }
            int tint = (hash8(tx / 4, ty / 4, 66u) & 23) - 12;
            put_texel(px, tx, ty, adj(base, tint));
        }
    }
}

/* bright layered water with sparkle texels */
static void gen_water_mc(uint32_t *px)
{
    const uint32_t light = COLOR_RGB(14, 175, 155);
    const uint32_t dark = COLOR_RGB(11, 138, 143);
    const uint32_t foam = COLOR_RGB(143, 248, 226);

    for (int ty = 0; ty < GRID; ty++) {
        for (int tx = 0; tx < GRID; tx++) {
            uint32_t c = (((tx + ty * 2) & 7) < 4) ? light : dark;
            if (hash8(tx, ty, 77u) > 245)
                c = foam;
            put_texel(px, tx, ty, c);
        }
    }
}

/* rotatable pillar block: chiseled sandstone with a bold arrow.
 * orient: 0=N(up) 1=E 2=S 3=W, drawn by rotating texel coordinates. */
static void gen_rotor(uint32_t *px, int orient)
{
    const uint32_t base = COLOR_RGB(226, 176, 106);
    const uint32_t carve = COLOR_RGB(122, 82, 40);
    const uint32_t edge = COLOR_RGB(249, 210, 140);

    for (int ty = 0; ty < GRID; ty++) {
        for (int tx = 0; tx < GRID; tx++) {
            /* rotate (tx,ty) back into "arrow points up" space */
            int ux = tx, uy = ty;
            switch (orient) {
            case 1: ux = ty;            uy = GRID - 1 - tx; break;
            case 2: ux = GRID - 1 - tx; uy = GRID - 1 - ty; break;
            case 3: ux = GRID - 1 - ty; uy = tx;            break;
            default: break;
            }

            int tint = (hash8(tx, ty, 88u) & 15) - 8;
            uint32_t c = adj(base, tint);
            if (tx == 0 || ty == 0) c = edge;
            else if (tx == GRID - 1 || ty == GRID - 1) c = carve;
            else {
                /* up-arrow: widening head + straight shaft */
                int head = uy >= 3 && uy <= 6 && abs(ux - 8) <= uy - 3;
                int shaft = uy >= 7 && uy <= 12 && abs(ux - 8) <= 1;
                if (head || shaft)
                    c = carve;
            }
            put_texel(px, tx, ty, c);
        }
    }
}

/* ---- sprites --------------------------------------------------------- */

/* value noise retained for organic things: statues, fire */
#define LATTICE 8

static void gen_noise(uint8_t out[TEX_SIZE * TEX_SIZE], Rng *r)
{
    uint8_t lat[TEX_SIZE / LATTICE][TEX_SIZE / LATTICE];
    const int n = TEX_SIZE / LATTICE;

    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            lat[y][x] = (uint8_t)rng_range(r, 256);

    for (int y = 0; y < TEX_SIZE; y++) {
        int cy = y / LATTICE, fy = y % LATTICE;
        for (int x = 0; x < TEX_SIZE; x++) {
            int cx = x / LATTICE, fx = x % LATTICE;
            int a = lat[cy][cx];
            int b = lat[cy][(cx + 1) % n];
            int c = lat[(cy + 1) % n][cx];
            int d = lat[(cy + 1) % n][(cx + 1) % n];
            int top = a * (LATTICE - fx) + b * fx;
            int bot = c * (LATTICE - fx) + d * fx;
            int v = (top * (LATTICE - fy) + bot * fy) / (LATTICE * LATTICE);
            out[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

static uint32_t modulate(uint32_t base, int factor)
{
    int k = 128 + (factor - 128);
    if (k < 0) k = 0;
    if (k > 255) k = 255;
    return color_scale(base, (uint8_t)k);
}

enum {
    PART_NONE, PART_PEDESTAL, PART_LEG, PART_TORSO,
    PART_ARM, PART_HEAD, PART_CROWN, PART_EYE
};

static int guardian_part(int x, int y)
{
    int hx = x < 32 ? 32 - 1 - x : x - 32;

    if (y >= 54 && hx <= 15) return PART_PEDESTAL;
    if (y >= 40 && y < 54 && hx >= 2 && hx <= 8) return PART_LEG;
    if (y >= 16 && y < 19 && hx <= 3) return hx >= 1 ? PART_EYE : PART_HEAD;
    if (y >= 11 && y < 24 && hx <= 5) return PART_HEAD;
    if (y >= 2 && y < 11 && hx <= 4 - y / 3) return PART_CROWN;
    if (y >= 22 && y < 42 && hx <= 10 - (y - 22) / 5) return PART_TORSO;
    if (y >= 24 && y < 46 && hx >= 9 && hx <= 13 - (y - 24) / 8)
        return PART_ARM;
    if (y >= 34 && y < 56 && hx <= 1) return PART_ARM;
    return PART_NONE;
}

static int yali_part(int x, int y)
{
    if (y >= 54 && x >= 8 && x < 56) return PART_PEDESTAL;
    if (y >= 42 && y < 54
        && ((x >= 14 && x < 19) || (x >= 24 && x < 29)
            || (x >= 36 && x < 41) || (x >= 46 && x < 51)))
        return PART_LEG;
    if (y >= 28 && y < 44 && x >= 12 && x < 50) return PART_TORSO;
    if (y >= 18 && y < 32 && x >= 48 && x < 58
        && (x - 52) * (x - 52) + (y - 25) * (y - 25) >= 9
        && (x - 52) * (x - 52) + (y - 25) * (y - 25) <= 30)
        return PART_ARM;
    if (y >= 12 && y < 34 && x >= 14 && x < 26) return PART_CROWN;
    if (y >= 14 && y < 30 && x >= 2 && x < 16
        && !(y >= 24 && y < 28 && x < 10))
        return (y >= 17 && y < 20 && x >= 5 && x < 8)
            ? PART_EYE : PART_HEAD;
    if (y >= 8 && y < 14 && x >= 8 && x < 12) return PART_CROWN;
    return PART_NONE;
}

static int naga_part(int x, int y)
{
    int hx = x < 32 ? 32 - 1 - x : x - 32;

    if (y >= 56 && hx <= 14) return PART_PEDESTAL;
    if (y >= 44 && y < 56) {
        int dy = y - 50;
        if (hx * hx / 4 + dy * dy <= 36) return PART_TORSO;
    }
    if (y >= 24 && y < 48) {
        int cx = 32 + (int)(4.0 * ((y - 24) & 15) / 15.0) - 2;
        int d = x < cx ? cx - x : x - cx;
        if (d <= 4) return PART_TORSO;
    }
    if (y >= 8 && y < 26) {
        int wgt = 12 - (y < 17 ? 17 - y : y - 17);
        if (wgt > 0 && hx <= wgt) {
            if (y >= 13 && y < 16 && hx >= 1 && hx <= 3)
                return PART_EYE;
            if (hx <= 3 && y >= 10 && y < 22) return PART_HEAD;
            return PART_CROWN;
        }
    }
    return PART_NONE;
}

static void gen_statue(uint32_t *px, Rng *r, int awake,
                       int (*part_of)(int, int), uint32_t skin,
                       uint32_t trim)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);

    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            int part = part_of(x, y);
            if (part == PART_NONE) {
                px[i] = 0;
                continue;
            }
            if (part == PART_EYE) {
                px[i] = awake ? COLOR_RGB(255, 154, 36)
                              : COLOR_RGB(72, 70, 66);
                continue;
            }
            int v = 115 + (noise[i] - 128) / 5 + (x - 32) / 4
                    + (x < 32 ? 8 : -8);
            uint32_t base;
            switch (part) {
            case PART_PEDESTAL: base = COLOR_RGB(120, 112, 104); break;
            case PART_CROWN:    base = trim; break;
            default:            base = skin; break;
            }
            if (awake)
                base = color_lerp(base, COLOR_RGB(206, 92, 16), 44);
            px[i] = modulate(base, v);
        }
    }
}

static void gen_lamp(uint32_t *px, Rng *r)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);

    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            int hx = x < 32 ? 32 - 1 - x : x - 32;
            px[i] = 0;

            if (y >= 46 && y < 56 && hx <= 8 - (y - 46) / 2)
                px[i] = modulate(COLOR_RGB(226, 176, 106),
                                 105 + (noise[i] - 128) / 5);
            else if (y >= 56 && y < 62 && hx <= 2)
                px[i] = COLOR_RGB(150, 104, 48);
            else if (y >= 62 && hx <= 6)
                px[i] = COLOR_RGB(122, 82, 40);
            else if (y >= 28 && y < 46) {
                int w = (46 - y) / 3;
                if (hx <= w) {
                    px[i] = (hx <= w - 2 && y > 34)
                        ? COLOR_RGB(255, 240, 150)
                        : COLOR_RGB(252, 154, 36);
                }
            }
        }
    }
}

/* unlit puzzle lamp with 'pips' order marks (1-3 gold dots) */
static void gen_puzzle_lamp(uint32_t *px, Rng *r, int pips)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);

    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            int hx = x < 32 ? 32 - 1 - x : x - 32;
            px[i] = 0;

            if (y >= 46 && y < 56 && hx <= 8 - (y - 46) / 2)
                px[i] = modulate(COLOR_RGB(160, 120, 70),
                                 95 + (noise[i] - 128) / 5);
            else if (y >= 56 && y < 62 && hx <= 2)
                px[i] = COLOR_RGB(122, 82, 40);
            else if (y >= 62 && hx <= 6)
                px[i] = COLOR_RGB(96, 62, 30);
            /* cold wick, no flame */
            else if (y >= 42 && y < 46 && hx <= 1)
                px[i] = COLOR_RGB(60, 52, 48);
        }
    }
    /* gold order pips floating above the bowl */
    int cx[3] = { 32, 24, 40 };
    for (int p = 0; p < pips && p < 3; p++) {
        int bx = cx[p];
        for (int y = 22; y < 28; y++)
            for (int x = bx - 3; x < bx + 3; x++)
                px[y * TEX_SIZE + x] = COLOR_RGB(249, 194, 43);
    }
}

static void gen_lever(uint32_t *px, int on)
{
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++)
        px[i] = 0;
    /* stone base */
    for (int y = 48; y < 62; y++)
        for (int x = 20; x < 44; x++)
            px[y * TEX_SIZE + x] =
                COLOR_RGB(150 + ((x ^ y) & 7), 140, 125);
    /* stick: diagonal left when off, right when on; brass knob */
    for (int s = 0; s < 20; s++) {
        int x = on ? 32 + s / 2 : 32 - s / 2;
        int y = 48 - s;
        for (int t = -2; t <= 2; t++)
            px[y * TEX_SIZE + x + t] = COLOR_RGB(96, 62, 30);
    }
    int kx = on ? 42 : 22, ky = 28;
    for (int y = ky - 4; y < ky + 4; y++)
        for (int x = kx - 4; x < kx + 4; x++)
            px[y * TEX_SIZE + x] = COLOR_RGB(249, 194, 43);
}

/* the sanctum shrine: a tiered bronze lamp, cold or ablaze */
static void gen_shrine(uint32_t *px, int lit)
{
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++)
        px[i] = 0;

    for (int y = 0; y < TEX_SIZE; y++) {
        int hx_max = -1;
        if (y >= 52) hx_max = 15;                 /* base */
        else if (y >= 44) hx_max = 10;            /* mid tier */
        else if (y >= 36) hx_max = 6;             /* top tier */
        else if (y >= 30) hx_max = 9;             /* bowl */
        if (hx_max < 0)
            continue;
        for (int x = 32 - hx_max; x < 32 + hx_max; x++) {
            int shade = 105 + ((x ^ y) & 15) - 8
                        + (x < 32 ? 10 : -10);
            px[y * TEX_SIZE + x] =
                modulate(COLOR_RGB(205, 140, 30), shade);
        }
    }
    if (lit) {
        /* great teardrop flame above the bowl */
        for (int y = 6; y < 30; y++) {
            int w = (30 - y) / 3;
            for (int x = 32 - w; x <= 32 + w; x++) {
                int hx = x < 32 ? 32 - x : x - 32;
                px[y * TEX_SIZE + x] = (hx <= w - 2 && y > 14)
                    ? COLOR_RGB(255, 240, 150)
                    : COLOR_RGB(252, 154, 36);
            }
        }
    }
}

static void gen_fireball(uint32_t *px, Rng *r, uint32_t core,
                         uint32_t mid, uint32_t outer)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);

    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            px[i] = 0;
            int dx = x - 32, dy = y - 36;
            int rr2 = dx * dx + dy * dy + (noise[i] - 128) / 3;
            if (rr2 <= 36)
                px[i] = core;
            else if (rr2 <= 110)
                px[i] = mid;
            else if (rr2 <= 210)
                px[i] = outer;
            else if (dy < 0 && dx * dx <= (36 + dy) * (36 + dy) / 16
                     && y > 6)
                px[i] = mid;
        }
    }
}

/* The Asura King: the guardian silhouette grown broad, horned and
 * fanged — a demon war-lord in stone, never a deity. */
static int boss_part(int x, int y)
{
    int hx = x < 32 ? 32 - 1 - x : x - 32;

    if (y >= 56 && hx <= 22) return PART_PEDESTAL;
    if (y >= 40 && y < 56 && hx >= 4 && hx <= 12) return PART_LEG;
    /* burning eyes, wide-set */
    if (y >= 14 && y < 18 && hx >= 2 && hx <= 5) return PART_EYE;
    /* fanged jaw: two teeth below the head */
    if (y >= 22 && y < 26 && (hx == 2 || hx == 5)) return PART_CROWN;
    if (y >= 8 && y < 24 && hx <= 8) return PART_HEAD;
    /* great curved horns */
    if (y >= 0 && y < 12 && hx >= 7 && hx <= 12 - y / 2)
        return PART_CROWN;
    /* tiered war crown */
    if (y >= 0 && y < 8 && hx <= 5 - y / 2) return PART_CROWN;
    /* massive torso */
    if (y >= 22 && y < 44 && hx <= 16 - (y - 22) / 4) return PART_TORSO;
    /* arms with a mace to each side */
    if (y >= 24 && y < 50 && hx >= 14 && hx <= 19 - (y - 24) / 6)
        return PART_ARM;
    if (y >= 30 && y < 58 && hx >= 18 && hx <= 21) return PART_ARM;
    return PART_NONE;
}

/* Asura bowman: a lean standing figure, bow held out to the left —
 * the bow arc is a ring test, the string a straight chord. */
static int archer_part(int x, int y)
{
    /* the bow: arc of a circle centred left of the figure */
    int bdx = x - 14, bdy = y - 30;
    int br2 = bdx * bdx + bdy * bdy;
    if (x <= 16 && y >= 12 && y <= 48 && br2 >= 196 && br2 <= 289)
        return PART_ARM; /* wooden limb of the bow */
    if (x == 17 && y >= 14 && y <= 46)
        return PART_ARM; /* the string */

    int hx = x < 34 ? 34 - 1 - x : x - 34; /* body centred at 34 */
    if (y >= 56 && hx <= 10) return PART_PEDESTAL;
    if (y >= 42 && y < 56 && hx >= 1 && hx <= 5) return PART_LEG;
    if (y >= 15 && y < 18 && hx <= 3) return hx >= 1 ? PART_EYE
                                                     : PART_HEAD;
    if (y >= 10 && y < 22 && hx <= 4) return PART_HEAD;
    if (y >= 4 && y < 10 && hx <= 3) return PART_CROWN; /* topknot */
    if (y >= 20 && y < 44 && hx <= 7 - (y - 20) / 8) return PART_TORSO;
    /* drawing arm reaching toward the bow */
    if (y >= 26 && y < 31 && x >= 17 && x <= 30) return PART_ARM;
    /* quiver across the back */
    if (y >= 18 && y < 34 && x >= 40 && x <= 44) return PART_CROWN;
    return PART_NONE;
}

/* Asura champion: broad armoured figure, round shield on the left
 * arm, straight blade raised on the right, crested helm. */
static int knight_part(int x, int y)
{
    /* round shield: filled disc left of the body */
    int sdx = x - 15, sdy = y - 34;
    if (sdx * sdx + sdy * sdy <= 100)
        return (sdx * sdx + sdy * sdy <= 16) ? PART_CROWN /* boss */
                                             : PART_ARM;
    /* raised sword on the right */
    if (x >= 48 && x <= 51 && y >= 6 && y <= 34) return PART_CROWN;
    if (x >= 45 && x <= 54 && y >= 34 && y <= 37) return PART_ARM;

    int hx = x < 32 ? 32 - 1 - x : x - 32;
    if (y >= 56 && hx <= 13) return PART_PEDESTAL;
    if (y >= 44 && y < 56 && hx >= 2 && hx <= 7) return PART_LEG;
    if (y >= 16 && y < 19 && hx <= 3) return hx >= 1 ? PART_EYE
                                                     : PART_HEAD;
    if (y >= 11 && y < 23 && hx <= 5) return PART_HEAD; /* helm */
    if (y >= 3 && y < 11 && hx <= 1) return PART_CROWN; /* crest */
    if (y >= 21 && y < 46 && hx <= 11 - (y - 21) / 6)
        return PART_TORSO; /* cuirass */
    return PART_NONE;
}

/* The keeper: the player's own body, drawn only in third person.
 * Same bones as the guardian, but flesh and cloth, and no pedestal. */
static int player_part(int x, int y)
{
    int hx = x < 32 ? 32 - 1 - x : x - 32;

    if (y >= 42 && y < 62 && hx >= 1 && hx <= 5) return PART_LEG;
    if (y >= 16 && y < 19 && hx <= 3) return PART_HEAD;
    if (y >= 10 && y < 23 && hx <= 4) return PART_HEAD;
    if (y >= 6 && y < 10 && hx <= 3) return PART_CROWN; /* headwrap */
    if (y >= 21 && y < 44 && hx <= 8 - (y - 21) / 8) return PART_TORSO;
    /* the vel carried upright at the right shoulder */
    if (x >= 44 && x <= 46 && y >= 8 && y <= 52) return PART_ARM;
    if (x >= 42 && x <= 48 && y >= 4 && y <= 10) return PART_CROWN;
    return PART_NONE;
}

static double seg_dist(double px_, double py_, double ax, double ay,
                       double bx, double by, double *u_out)
{
    double vx = bx - ax, vy = by - ay;
    double len2 = vx * vx + vy * vy;
    double u = ((px_ - ax) * vx + (py_ - ay) * vy) / len2;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    double dx = px_ - (ax + u * vx), dy = py_ - (ay + u * vy);
    *u_out = u;
    return dx * dx + dy * dy;
}

static void gen_vel(uint32_t *px, Rng *r)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);

    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            px[i] = 0;
            double u;

            double d2 = seg_dist(x, y, 26, 20, 9, 3, &u);
            double leaf = 5.5 * 2.0 * sqrt(u * (1.0 - u) + 1e-9);
            if (d2 <= leaf * leaf) {
                int bright = d2 <= 1.2 ? 150 : 110;
                px[i] = modulate(COLOR_RGB(199, 220, 208),
                                 bright + (noise[i] - 128) / 8);
                continue;
            }
            d2 = seg_dist(x, y, 30, 25, 26, 20, &u);
            if (d2 <= 2.6 * 2.6) {
                px[i] = COLOR_RGB(226, 176, 106);
                continue;
            }
            d2 = seg_dist(x, y, 62, 63, 28, 23, &u);
            if (d2 <= 2.0 * 2.0)
                px[i] = modulate(COLOR_RGB(122, 82, 40),
                                 110 + (noise[i] - 128) / 6);
        }
    }
}

/* ---- items ----------------------------------------------------------- */

static void gen_sword(uint32_t *px, Rng *r)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);
    for (int y = 0; y < TEX_SIZE; y++)
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            px[i] = 0;
            double u;
            /* straight blade on the same diagonal as the vel */
            double d2 = seg_dist(x, y, 40, 40, 10, 8, &u);
            if (d2 <= 2.6 * 2.6) {
                px[i] = modulate(COLOR_RGB(199, 220, 208),
                                 d2 <= 1.0 ? 150 : 112
                                 + (noise[i] - 128) / 8);
                continue;
            }
            /* crossguard perpendicular to the blade */
            d2 = seg_dist(x, y, 36, 48, 48, 38, &u);
            if (d2 <= 2.2 * 2.2) {
                px[i] = COLOR_RGB(249, 194, 43);
                continue;
            }
            /* grip and pommel */
            d2 = seg_dist(x, y, 44, 45, 52, 54, &u);
            if (d2 <= 2.0 * 2.0)
                px[i] = COLOR_RGB(122, 82, 40);
            d2 = seg_dist(x, y, 54, 56, 54, 56, &u);
            if (d2 <= 3.0 * 3.0)
                px[i] = COLOR_RGB(249, 194, 43);
        }
}

static void gen_bow(uint32_t *px, Rng *r)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);
    for (int y = 0; y < TEX_SIZE; y++)
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            px[i] = 0;
            /* the stave: an arc of a circle centred to the left */
            int dx = x - 12, dy = y - 32;
            int r2 = dx * dx + dy * dy;
            if (x >= 12 && y >= 8 && y <= 56 && r2 >= 484 && r2 <= 625)
                px[i] = modulate(COLOR_RGB(122, 82, 40),
                                 110 + (noise[i] - 128) / 6);
            /* the string: a straight chord between the tips */
            else if (x >= 33 && x <= 34 && y >= 11 && y <= 53)
                px[i] = COLOR_RGB(222, 210, 190);
        }
}

static void gen_shield(uint32_t *px, Rng *r)
{
    uint8_t noise[TEX_SIZE * TEX_SIZE];
    gen_noise(noise, r);
    for (int y = 0; y < TEX_SIZE; y++)
        for (int x = 0; x < TEX_SIZE; x++) {
            int i = y * TEX_SIZE + x;
            px[i] = 0;
            int dx = x - 32, dy = y - 32;
            int r2 = dx * dx + dy * dy;
            if (r2 > 24 * 24)
                continue;
            if (r2 > 21 * 21)
                px[i] = COLOR_RGB(122, 82, 40);       /* rim */
            else if (r2 <= 36)
                px[i] = COLOR_RGB(249, 194, 43);      /* boss */
            else if (r2 <= 64)
                px[i] = COLOR_RGB(150, 104, 48);
            else
                px[i] = modulate(COLOR_RGB(205, 140, 30),
                                 105 + (noise[i] - 128) / 6
                                 + (dx - dy) / 3);    /* brass face */
        }
}

/* Minecraft-flask potion: round body, short neck, cork, coloured
 * liquid with a light shine texel. */
static void gen_potion(uint32_t *px, uint32_t liquid)
{
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++)
        px[i] = 0;
    for (int y = 0; y < TEX_SIZE; y++)
        for (int x = 0; x < TEX_SIZE; x++) {
            int dx = x - 32, dy = y - 40;
            int body = dx * dx + dy * dy <= 15 * 15;
            int neck = y >= 14 && y < 28 && dx >= -4 && dx <= 4;
            int cork = y >= 8 && y < 14 && dx >= -5 && dx <= 5;
            if (cork) {
                px[y * TEX_SIZE + x] = COLOR_RGB(122, 82, 40);
            } else if (body || neck) {
                /* liquid fills the lower part; glass above it */
                uint32_t c = (y >= 34) ? liquid
                                       : COLOR_RGB(155, 171, 178);
                /* rim + shine */
                if (body && dx * dx + dy * dy >= 13 * 13)
                    c = COLOR_RGB(199, 220, 208);
                if (dx >= -9 && dx <= -6 && dy >= -6 && dy <= -3)
                    c = COLOR_RGB(255, 255, 255);
                px[y * TEX_SIZE + x] = c;
            }
        }
}

/* Minecraft-style chest, on the 16-texel grid like the blocks */
static void gen_chest(uint32_t *px, int open)
{
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++)
        px[i] = 0;
    const uint32_t wood = COLOR_RGB(158, 100, 46);
    const uint32_t band = COLOR_RGB(84, 52, 24);
    const uint32_t gold = COLOR_RGB(249, 194, 43);

    for (int ty = 4; ty < GRID; ty++) {
        for (int tx = 1; tx < GRID - 1; tx++) {
            uint32_t c = adj(wood, (hash8(tx, ty, 99u) & 15) - 8);
            /* dark iron banding along every edge */
            if (tx == 1 || tx == GRID - 2 || ty == 4 || ty == GRID - 1
                || ty == 8)
                c = band;
            /* an open chest shows its dark inside above the lid line */
            if (open && ty < 8 && tx > 2 && tx < GRID - 3)
                c = COLOR_RGB(24, 18, 14);
            /* the latch */
            if (tx >= 7 && tx <= 8 && ty >= 7 && ty <= 9)
                c = open ? band : gold;
            put_texel(px, tx, ty, c);
        }
    }
}

static void gen_arrow(uint32_t *px)
{
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++)
        px[i] = 0;
    /* small horizontal arrow: head, shaft, fletching */
    for (int x = 14; x < 46; x++)
        for (int y = 30; y < 33; y++)
            px[y * TEX_SIZE + x] = COLOR_RGB(122, 82, 40);
    for (int s = 0; s < 6; s++)
        for (int y = 31 - s / 2; y <= 31 + s / 2; y++)
            px[y * TEX_SIZE + 46 + s] = COLOR_RGB(155, 171, 178);
    for (int s = 0; s < 5; s++)
        for (int y = 29 - s; y <= 33 + s; y += (2 * s ? 2 * s : 1))
            px[y * TEX_SIZE + 14 + s] = COLOR_RGB(222, 210, 190);
}

int textures_init(void)
{
    tex_data = malloc((size_t)TEX_COUNT * TEX_SIZE * TEX_SIZE
                      * sizeof *tex_data);
    if (!tex_data)
        return -1;

    Rng r;
    rng_seed(&r, 0x54454D50u); /* fixed seed: reproducible visuals */

    /* blocks */
    gen_stone_brick(slot(TEX_GRANITE));
    gen_chiseled(slot(TEX_PILLAR));
    gen_sanctum_brick(slot(TEX_SANCTUM));
    gen_door_planks(slot(TEX_DOOR));
    gen_slab_floor(slot(TEX_FLOOR));
    gen_coffer_ceiling(slot(TEX_CEILING));
    gen_water_mc(slot(TEX_WATER));
    for (int o = 0; o < 4; o++)
        gen_rotor(slot((TextureId)(TEX_ROTOR_N + o)), o);

    /* statues: each pair reseeded so waking doesn't reshuffle grain */
    Rng gr;
    rng_seed(&gr, 0xD7A2A1A4u);
    gen_statue(slot(TEX_GUARDIAN), &gr, 0, guardian_part,
               COLOR_RGB(158, 150, 138), COLOR_RGB(206, 152, 84));
    rng_seed(&gr, 0xD7A2A1A4u);
    gen_statue(slot(TEX_GUARDIAN_AWAKE), &gr, 1, guardian_part,
               COLOR_RGB(158, 150, 138), COLOR_RGB(206, 152, 84));
    rng_seed(&gr, 0x59414C49u);
    gen_statue(slot(TEX_YALI), &gr, 0, yali_part,
               COLOR_RGB(158, 150, 138), COLOR_RGB(196, 132, 62));
    rng_seed(&gr, 0x59414C49u);
    gen_statue(slot(TEX_YALI_AWAKE), &gr, 1, yali_part,
               COLOR_RGB(158, 150, 138), COLOR_RGB(196, 132, 62));
    rng_seed(&gr, 0x4E414741u);
    gen_statue(slot(TEX_NAGA), &gr, 0, naga_part,
               COLOR_RGB(96, 148, 112), COLOR_RGB(58, 100, 74));
    rng_seed(&gr, 0x4E414741u);
    gen_statue(slot(TEX_NAGA_AWAKE), &gr, 1, naga_part,
               COLOR_RGB(96, 148, 112), COLOR_RGB(58, 100, 74));

    /* the king in dark red stone with gold horns and crown */
    rng_seed(&gr, 0xA5A2A0B5u);
    gen_statue(slot(TEX_BOSS), &gr, 0, boss_part,
               COLOR_RGB(118, 60, 34), COLOR_RGB(249, 194, 43));
    rng_seed(&gr, 0xA5A2A0B5u);
    gen_statue(slot(TEX_BOSS_AWAKE), &gr, 1, boss_part,
               COLOR_RGB(118, 60, 34), COLOR_RGB(249, 194, 43));

    /* the war-band: bowman in jungle green cloth, champion in
     * verdigris bronze armour */
    rng_seed(&gr, 0xB0DE5A17u);
    gen_statue(slot(TEX_ARCHER), &gr, 0, archer_part,
               COLOR_RGB(128, 108, 88), COLOR_RGB(86, 122, 62));
    rng_seed(&gr, 0xB0DE5A17u);
    gen_statue(slot(TEX_ARCHER_AWAKE), &gr, 1, archer_part,
               COLOR_RGB(128, 108, 88), COLOR_RGB(86, 122, 62));
    rng_seed(&gr, 0xC4A7A11Du);
    gen_statue(slot(TEX_KNIGHT), &gr, 0, knight_part,
               COLOR_RGB(106, 156, 120), COLOR_RGB(226, 176, 106));
    rng_seed(&gr, 0xC4A7A11Du);
    gen_statue(slot(TEX_KNIGHT_AWAKE), &gr, 1, knight_part,
               COLOR_RGB(106, 156, 120), COLOR_RGB(226, 176, 106));

    /* the keeper in warm skin and saffron cloth (never "awake") */
    rng_seed(&gr, 0x6B335F31u);
    gen_statue(slot(TEX_PLAYER), &gr, 0, player_part,
               COLOR_RGB(171, 122, 84), COLOR_RGB(247, 150, 23));

    gen_lamp(slot(TEX_LAMP), &r);
    gen_fireball(slot(TEX_FLAMEBALL), &r,
                 COLOR_RGB(255, 240, 150), COLOR_RGB(254, 162, 44),
                 COLOR_RGB(206, 92, 16));
    gen_fireball(slot(TEX_VENOM), &r,
                 COLOR_RGB(213, 224, 75), COLOR_RGB(145, 219, 105),
                 COLOR_RGB(35, 144, 99));
    gen_vel(slot(TEX_VEL), &r);

    gen_puzzle_lamp(slot(TEX_PLAMP_1), &r, 1);
    gen_puzzle_lamp(slot(TEX_PLAMP_2), &r, 2);
    gen_puzzle_lamp(slot(TEX_PLAMP_3), &r, 3);
    gen_lever(slot(TEX_LEVER_OFF), 0);
    gen_lever(slot(TEX_LEVER_ON), 1);
    gen_shrine(slot(TEX_SHRINE_OFF), 0);
    gen_shrine(slot(TEX_SHRINE_ON), 1);

    gen_sword(slot(TEX_SWORD), &r);
    gen_bow(slot(TEX_BOW), &r);
    gen_shield(slot(TEX_SHIELD), &r);
    gen_potion(slot(TEX_POT_HEAL), COLOR_RGB(232, 59, 59));
    gen_potion(slot(TEX_POT_STR), COLOR_RGB(251, 107, 29));
    gen_potion(slot(TEX_POT_SPD), COLOR_RGB(48, 225, 185));
    gen_chest(slot(TEX_CHEST), 0);
    gen_chest(slot(TEX_CHEST_OPEN), 1);
    gen_arrow(slot(TEX_ARROW));
    return 0;
}

void textures_shutdown(void)
{
    free(tex_data);
    tex_data = NULL;
}
