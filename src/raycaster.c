#include "raycaster.h"

#include <math.h>

#include "texture.h"

/* Fog: distant stone dissolves into warm dusty haze — sunlit ruin,
 * not midnight dungeon. */
#define FOG_DIST 26.0
#define FOG_COLOR COLOR_RGB(120, 100, 74)

/* Wall height in world units (a tile is 1.0 wide). Raised above 1.0
 * so the halls tower a little — the third-person camera especially
 * needs headroom to read as a place rather than a corridor of crates.
 * The wall texture simply repeats vertically past 1.0. */
#define WALL_H 1.4

/* Per-column perpendicular wall distance from the last rendered frame.
 * The sprite pass clips against this: a sprite column only draws where
 * the sprite is nearer than zbuffer[x]. */
static double zbuffer[FB_WIDTH];

const double *raycast_depth(void)
{
    return zbuffer;
}

static double flicker = 1.0;

void raycast_set_flicker(double f)
{
    flicker = f;
}

/* ---- lighting --------------------------------------------------------
 * Lamplight model: brightness falls off with a 1/(quadratic) curve —
 * bright and warm within a few tiles, dropping fast, never fully black
 * — then the remainder is fogged toward deep blue. The expensive
 * double math happens once per column/row/sprite; per PIXEL it is only
 * an integer scale and lerp. min_bright lets self-lit things (flames,
 * burning eyes) ignore the darkness: it lifts brightness AND cuts fog. */
typedef struct {
    uint8_t bright;
    uint8_t fog;
} Shade;

static Shade shade_for(double dist, int darker_face, uint8_t min_bright)
{
    /* Minecraft-bright: high ambient, gentle falloff, strong face
     * shading so every block edge reads. Never near-dark. */
    double b = 255.0 / (1.0 + 0.03 * dist + 0.005 * dist * dist);
    b *= flicker;
    if (darker_face)
        b *= 0.80;
    if (b < 90.0)
        b = 90.0;
    if (b > 255.0)
        b = 255.0;
    if (b < min_bright)
        b = min_bright;

    /* fog eases in with a power curve: near/mid walls stay crisp and
     * readable, the haze only takes over at real distance */
    double t = dist / FOG_DIST;
    if (t > 1.0)
        t = 1.0;
    t = pow(t, 1.6);
    t *= (double)(255 - min_bright) / 255.0;

    Shade s;
    s.bright = (uint8_t)b;
    s.fog = (uint8_t)(t * 255.0);
    return s;
}

static uint32_t shade_apply(uint32_t c, Shade s)
{
    return color_lerp(color_scale(c, s.bright), FOG_COLOR, s.fog);
}

/* Which texture each wall tile uses. Indexed by TileType; rotors pick
 * their orientation frame from the map's meta layer at render time. */
static const TextureId tile_tex[] = {
    TEX_GRANITE, /* TILE_FLOOR — never hit as a wall */
    TEX_GRANITE,
    TEX_PILLAR,
    TEX_SANCTUM,
    TEX_DOOR,
    TEX_ROTOR_N, /* TILE_ROTOR base; + meta gives the orientation */
};

/* ---- floor and ceiling casting --------------------------------------
 * Walls are drawn per COLUMN; floors per ROW, using one fact: every
 * floor pixel on a given screen row is the same distance away. For
 * that row we intersect the left-edge and right-edge camera rays with
 * the floor plane and linearly step the world position across the
 * row, sampling the texture at each step. */
static double anim_time = 0.0;

void raycast_set_time(double t)
{
    anim_time = t;
}

static void cast_floor_ceiling(Framebuffer *fb, const Map *m,
                               const Player *p)
{
    const uint32_t *floor_tex = texture_pixels(TEX_FLOOR);
    const uint32_t *water_tex = texture_pixels(TEX_WATER);
    const uint32_t *ceil_tex = texture_pixels(TEX_CEILING);
    const int w = fb->w, h = fb->h;

    /* water scrolls its texture coordinates over time; two different
     * speeds on the two axes so the drift never looks mechanical */
    double wave_u = anim_time * 9.0;
    double wave_v = anim_time * 5.5;

    /* ray directions at the two screen edges */
    double rx0 = p->dirX - p->planeX, ry0 = p->dirY - p->planeY;
    double rx1 = p->dirX + p->planeX, ry1 = p->dirY + p->planeY;

    /* Looking up/down shifts the horizon line; jumping raises the
     * camera. cz is camera height in wall units (0.5 = eye level). */
    int horizon = h / 2 + (int)p->pitch;
    double cz = 0.5 + p->z;

    for (int y = horizon + 1; y < h; y++) {
        /* Similar triangles: the camera is cz wall-heights above the
         * floor and this row is (y - horizon) pixels below the
         * horizon, so the floor seen here is at this distance: */
        double rowDist = (cz * h) / (y - horizon);

        Shade sh = shade_for(rowDist, 0, 0);
        double fx = p->x + rowDist * rx0;
        double fy = p->y + rowDist * ry0;
        double stepX = rowDist * (rx1 - rx0) / w;
        double stepY = rowDist * (ry1 - ry0) / w;

        uint32_t *fl = fb->pixels + y * w;
        for (int x = 0; x < w; x++) {
            /* fractional part of the world position picks the texel;
             * & TEX_MASK wraps because the size is a power of two */
            if (map_floor(m, (int)fx, (int)fy) == FLOOR_WATER) {
                int wx = (int)((fx - floor(fx)) * TEX_SIZE + wave_u)
                         & TEX_MASK;
                int wy = (int)((fy - floor(fy)) * TEX_SIZE + wave_v)
                         & TEX_MASK;
                fl[x] = shade_apply(water_tex[wy * TEX_SIZE + wx], sh);
            } else {
                int tx = (int)((fx - floor(fx)) * TEX_SIZE) & TEX_MASK;
                int ty = (int)((fy - floor(fy)) * TEX_SIZE) & TEX_MASK;
                fl[x] = shade_apply(floor_tex[ty * TEX_SIZE + tx], sh);
            }
            fx += stepX;
            fy += stepY;
        }
    }

    /* ceiling: the same construction reflected above the horizon,
     * with the camera (WALL_H - cz) below the ceiling plane */
    for (int y = 0; y < horizon && y < h; y++) {
        double rowDist = ((WALL_H - cz) * h) / (horizon - y);

        Shade sh = shade_for(rowDist, 0, 0);
        double fx = p->x + rowDist * rx0;
        double fy = p->y + rowDist * ry0;
        double stepX = rowDist * (rx1 - rx0) / w;
        double stepY = rowDist * (ry1 - ry0) / w;

        uint32_t *ce = fb->pixels + y * w;
        for (int x = 0; x < w; x++) {
            int tx = (int)((fx - floor(fx)) * TEX_SIZE) & TEX_MASK;
            int ty = (int)((fy - floor(fy)) * TEX_SIZE) & TEX_MASK;
            fx += stepX;
            fy += stepY;
            uint32_t c = color_scale(ceil_tex[ty * TEX_SIZE + tx], 205);
            ce[x] = shade_apply(c, sh);
        }
    }
}

void raycast_render(Framebuffer *fb, const Map *m, const Player *p)
{
    cast_floor_ceiling(fb, m, p);

    for (int x = 0; x < fb->w; x++) {
        /* cameraX sweeps -1..+1 across the screen: -1 is the left edge of
         * the camera plane, +1 the right. The ray for this column is the
         * view direction plus that fraction of the plane vector. */
        double cameraX = 2.0 * x / fb->w - 1.0;
        double rayX = p->dirX + p->planeX * cameraX;
        double rayY = p->dirY + p->planeY * cameraX;

        int mapX = (int)p->x;
        int mapY = (int)p->y;

        /* deltaDist: how far along the ray between two successive
         * vertical (X) or horizontal (Y) grid lines. 1e30 stands in
         * for infinity when a component is zero. */
        double deltaDistX = (rayX == 0.0) ? 1e30 : fabs(1.0 / rayX);
        double deltaDistY = (rayY == 0.0) ? 1e30 : fabs(1.0 / rayY);

        int stepX, stepY;
        double sideDistX, sideDistY;

        if (rayX < 0) {
            stepX = -1;
            sideDistX = (p->x - mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = (mapX + 1.0 - p->x) * deltaDistX;
        }
        if (rayY < 0) {
            stepY = -1;
            sideDistY = (p->y - mapY) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = (mapY + 1.0 - p->y) * deltaDistY;
        }

        /* DDA: repeatedly step into the nearer of the two next grid
         * lines; visits every cell on the ray's path in order. */
        int side = 0; /* 0 = crossed a vertical line, 1 = horizontal */
        while (!map_solid(m, mapX, mapY)) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
        }

        /* Perpendicular distance to the camera plane, not the true ray
         * length — ray length would bow straight walls outward
         * (fisheye), because edge rays are longer than the centre ray
         * even for a flat wall. */
        double perpDist = (side == 0)
            ? sideDistX - deltaDistX
            : sideDistY - deltaDistY;
        if (perpDist < 1e-6)
            perpDist = 1e-6;
        zbuffer[x] = perpDist;

        /* Project the wall's top (world height WALL_H) and bottom (0)
         * with the pitched horizon and jump-raised camera at height cz:
         * screen_y = horizon + (cz - world_z) * h / dist. */
        int horizon = fb->h / 2 + (int)p->pitch;
        double cz = 0.5 + p->z;
        int wallTop = horizon + (int)((cz - WALL_H) * fb->h / perpDist);
        int wallBot = horizon + (int)(cz * fb->h / perpDist);
        int lineH = wallBot - wallTop;
        if (lineH < 1)
            lineH = 1;
        int drawStart = wallTop < 0 ? 0 : wallTop;
        int drawEnd = wallBot > fb->h ? fb->h : wallBot;

        /* wallX: where along the tile face the ray hit, 0..1. Found by
         * pushing the player position along the ray to the wall and
         * keeping the coordinate that runs ALONG the wall. */
        double wallX = (side == 0)
            ? p->y + perpDist * rayY
            : p->x + perpDist * rayX;
        wallX -= floor(wallX);

        int texX = (int)(wallX * TEX_SIZE);
        /* Two of the four wall orientations would show the texture
         * mirrored; flip texX for those so writing wraps consistently
         * around a pillar. */
        if ((side == 0 && rayX > 0) || (side == 1 && rayY < 0))
            texX = TEX_SIZE - 1 - texX;

        uint8_t hit_tile = map_tile(m, mapX, mapY);
        TextureId tid = tile_tex[hit_tile];
        if (hit_tile == TILE_ROTOR)
            tid = (TextureId)(tid + map_meta(m, mapX, mapY));
        const uint32_t *tex = texture_pixels(tid);

        Shade sh = shade_for(perpDist, side == 1, 0);

        /* Step through texture rows so ONE texture repeat covers ONE
         * world unit of height — the extra 0.4 above wraps around
         * (& TEX_MASK below), keeping the block texels square instead
         * of stretching them over the taller wall. If the slice
         * overflows the screen, start partway into the texture. */
        double texStep = (double)TEX_SIZE * WALL_H / lineH;
        double texPos = (drawStart - wallTop) * texStep;

        /* darken the outermost texture columns so each wall block has
         * a crisp visible edge instead of melting into its neighbour */
        int at_edge = (texX <= 1 || texX >= TEX_SIZE - 2);

        uint32_t *px = fb->pixels + drawStart * fb->w + x;
        for (int y = drawStart; y < drawEnd; y++) {
            int texY = (int)texPos & TEX_MASK;
            texPos += texStep;

            uint32_t c = tex[texY * TEX_SIZE + texX];
            if (at_edge)
                c = color_scale(c, 170);
            *px = shade_apply(c, sh);
            px += fb->w;
        }
    }
}

/* ---- billboard sprites ---------------------------------------------- */

void raycast_sprites(Framebuffer *fb, const Player *p,
                     const EntityList *el)
{
    /* Painter's algorithm: sort farthest-first so near sprites are
     * drawn over far ones. Insertion sort — hand-written, and ideal
     * here because the order barely changes between frames, which
     * makes it near O(n) on already-sorted input. */
    typedef struct {
        double dist2;
        const Entity *e;
    } Ref;
    Ref refs[64];
    int n = el->count < 64 ? el->count : 64;

    for (int i = 0; i < n; i++) {
        const Entity *e = &el->items[i];
        double dx = e->x - p->x, dy = e->y - p->y;
        Ref r = { dx * dx + dy * dy, e };
        int j = i;
        while (j > 0 && refs[j - 1].dist2 < r.dist2) {
            refs[j] = refs[j - 1];
            j--;
        }
        refs[j] = r;
    }

    const int w = fb->w, h = fb->h;

    for (int i = 0; i < n; i++) {
        const Entity *e = refs[i].e;
        double sx = e->x - p->x;
        double sy = e->y - p->y;

        /* Inverse camera transform. The camera matrix has columns
         * (planeX, planeY) and (dirX, dirY); multiplying by its
         * inverse re-expresses the sprite offset in camera terms:
         * ty = depth straight ahead, tx = sideways offset. */
        double invDet =
            1.0 / (p->planeX * p->dirY - p->dirX * p->planeY);
        double tx = invDet * (p->dirY * sx - p->dirX * sy);
        double ty = invDet * (-p->planeY * sx + p->planeX * sy);

        if (ty <= 0.08)
            continue; /* behind, or on top of, the camera */

        /* same similar-triangles rule as walls: size = height/depth,
         * and the same pitched-horizon projection for placement */
        int size = (int)(h / ty);
        if (size <= 0)
            continue;

        int horizon = h / 2 + (int)p->pitch;
        double cz = 0.5 + p->z;
        /* per-entity size: the boss stands two walls tall */
        double hgt = e->height > 0.0 ? e->height : 1.0;
        double wm = e->widthm > 0.0 ? e->widthm : 1.0;

        /* ---- gait ----
         * e->anim advances with distance walked, so these read as
         * steps, not idle jitter. Serpents get a lateral S-slither
         * hugging the ground; the yali a bounding gallop with
         * squash-and-stretch; everything on two legs a step-bob. */
        double bob = 0.0, sway = 0.0;
        int moving = (ENTITY_IS_ENEMY(e) && e->state != ST_DORMANT
                      && e->state != ST_DYING)
                     || e->tex == TEX_PLAYER; /* third-person keeper */
        if (moving) {
            double a = e->anim;
            switch (e->type) {
            case ENT_NAGA:
                sway = sin(a) * 0.11;               /* the S-curve */
                hgt *= 1.0 + 0.07 * sin(a * 2.0);   /* body ripple */
                break;
            case ENT_YALI:
                bob = fabs(sin(a * 0.65)) * 0.09;   /* long bounds */
                hgt *= 1.0 - 0.10 * fabs(cos(a * 0.65)); /* gather */
                break;
            default:                                /* two legs */
                bob = fabs(sin(a)) * 0.045;
                sway = sin(a * 0.5) * 0.03;
                break;
            }
        }

        int screenX = (int)(w / 2.0 * (1.0 + tx / ty)
                            + sway * h / ty);
        int lift = (int)(bob * h / ty);
        int wsize = (int)(size * wm);
        if (wsize < 1)
            continue;
        int x0 = screenX - wsize / 2, x1 = x0 + wsize;
        int y0 = horizon + (int)((cz - hgt) * h / ty) - lift;
        int y1 = horizon + (int)(cz * h / ty) - lift;
        int vsize = y1 - y0;
        if (vsize < 1)
            continue;
        int cx0 = x0 < 0 ? 0 : x0;
        int cx1 = x1 > w ? w : x1;
        int cy0 = y0 < 0 ? 0 : y0;
        int cy1 = y1 > h ? h : y1;

        const uint32_t *tex = texture_pixels((TextureId)e->tex);
        Shade sh = shade_for(ty, 0, e->glow);

        for (int x = cx0; x < cx1; x++) {
            /* the whole column is skipped when a wall is nearer */
            if (ty >= zbuffer[x])
                continue;
            int texX = (x - x0) * TEX_SIZE / wsize;

            for (int y = cy0; y < cy1; y++) {
                int texY = (y - y0) * TEX_SIZE / vsize;
                uint32_t c = tex[texY * TEX_SIZE + texX];
                if ((c >> 24) == 0)
                    continue; /* transparent texel */
                /* Minecraft-style hurt flash: tint the mob red */
                if (e->hurt_t > 0.0)
                    c = color_lerp(c, COLOR_RGB(232, 59, 59), 140);
                fb->pixels[y * w + x] = shade_apply(c, sh);
            }
        }

        /* floating health bar over any enemy that has joined the
         * fight, drawn only while its centre column is visible */
        if (ENTITY_IS_ENEMY(e) && e->state != ST_DORMANT
            && e->state != ST_DYING && e->maxhp > 0
            && screenX >= 0 && screenX < w && ty < zbuffer[screenX]) {
            int bw = wsize > 36 ? 36 : wsize;
            if (bw >= 8) {
                int bx = screenX - bw / 2;
                int by = y0 - 5;
                if (by >= 0) {
                    int fill = bw * e->hp / e->maxhp;
                    uint32_t col = e->hp * 3 > e->maxhp
                        ? COLOR_RGB(35, 144, 99)
                        : COLOR_RGB(232, 59, 59);
                    fb_fill_rect(fb, bx - 1, by - 1, bw + 2, 4,
                                 COLOR_RGB(20, 16, 14));
                    fb_fill_rect(fb, bx, by, fill, 2, col);
                }
            }
        }
    }
}
