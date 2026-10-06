#include "map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* ---- queries --------------------------------------------------------- */

uint8_t map_tile(const Map *m, int x, int y)
{
    if (x < 0 || x >= m->w || y < 0 || y >= m->h)
        return TILE_GRANITE;
    return m->tiles[y * m->w + x];
}

uint8_t map_floor(const Map *m, int x, int y)
{
    if (x < 0 || x >= m->w || y < 0 || y >= m->h)
        return FLOOR_STONE;
    return m->floors[y * m->w + x];
}

uint8_t map_meta(const Map *m, int x, int y)
{
    if (x < 0 || x >= m->w || y < 0 || y >= m->h)
        return 0;
    return m->meta[y * m->w + x];
}

int map_solid(const Map *m, int x, int y)
{
    return map_tile(m, x, y) != TILE_FLOOR;
}

int map_walkable(const Map *m, int x, int y)
{
    return !map_solid(m, x, y)
        && map_floor(m, x, y) != FLOOR_WATER;
}

/* One bit per tile: index i lives in byte i>>3, at bit i&7. Storing
 * bits instead of bytes cuts the layer to an eighth of the size and
 * is the classic use of shifts and masks. */
int map_seen(const Map *m, int x, int y)
{
    if (x < 0 || x >= m->w || y < 0 || y >= m->h)
        return 0;
    int i = y * m->w + x;
    return (m->seen[i >> 3] >> (i & 7)) & 1;
}

void map_reveal(Map *m, double x, double y, double radius)
{
    int r = (int)radius;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int tx = (int)x + dx, ty = (int)y + dy;
            if (tx < 0 || tx >= m->w || ty < 0 || ty >= m->h)
                continue;
            if (dx * dx + dy * dy > radius * radius)
                continue;
            int i = ty * m->w + tx;
            m->seen[i >> 3] |= (uint8_t)(1u << (i & 7));
        }
}

int map_los(const Map *m, double x0, double y0, double x1, double y1)
{
    int mapX = (int)x0, mapY = (int)y0;
    const int endX = (int)x1, endY = (int)y1;

    double dx = x1 - x0, dy = y1 - y0;
    double deltaX = (dx == 0.0) ? 1e30 : fabs(1.0 / dx);
    double deltaY = (dy == 0.0) ? 1e30 : fabs(1.0 / dy);

    int stepX = dx < 0 ? -1 : 1;
    int stepY = dy < 0 ? -1 : 1;
    double sideX = (dx < 0 ? (x0 - mapX) : (mapX + 1.0 - x0)) * deltaX;
    double sideY = (dy < 0 ? (y0 - mapY) : (mapY + 1.0 - y0)) * deltaY;

    /* Walk cell to cell exactly like the renderer's DDA, but stop at
     * the target cell instead of at the first wall. */
    while (mapX != endX || mapY != endY) {
        if (sideX < sideY) {
            sideX += deltaX;
            mapX += stepX;
        } else {
            sideY += deltaY;
            mapY += stepY;
        }
        if (map_solid(m, mapX, mapY))
            return 0;
    }
    return 1;
}

/* ---- generation ------------------------------------------------------ */

typedef struct {
    int x, y, w, h;
} Rect;

#define MAX_ROOMS 32

/* per-style generation parameters: the "multiple layouts" knob */
typedef struct {
    int depth;        /* BSP recursion depth */
    int min_split;    /* stop splitting below this size */
    int wide_halls;   /* 2-tile corridors? */
    int pillar_step;  /* pillar grid spacing (0 = almost none) */
} StyleParams;

static const StyleParams styles[] = {
    { 5, 14, 1, 3 }, /* STYLE_HALLS */
    { 6, 10, 0, 4 }, /* STYLE_MAZE: more, smaller, tighter */
    { 4, 17, 1, 2 }, /* STYLE_CITADEL: vast chambers, pillar forests */
};

typedef struct {
    Map *m;
    Rng rng;
    Rect rooms[MAX_ROOMS];
    int nrooms;
    int sanctum; /* index into rooms, -1 until chosen */
    StyleParams sp;
    int with_boss;
} Gen;

/* rng_range that tolerates n == 0 (returns 0), for jittered sizes */
static uint32_t rr(Rng *r, uint32_t n)
{
    return n ? rng_range(r, n) : 0;
}

static void carve(Map *m, int x, int y)
{
    /* the outermost ring is never carved: the world stays sealed */
    if (x > 0 && y > 0 && x < m->w - 1 && y < m->h - 1)
        m->tiles[y * m->w + x] = TILE_FLOOR;
}

static void carve_room(Map *m, Rect r)
{
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            carve(m, x, y);
}

/* Smallest half a split may leave behind. A leaf thinner than the
 * minimum room size used to underflow the sizing arithmetic below,
 * so the split rule and the room rule have to agree on this number. */
#define MIN_HALF 6

/* Recursive BSP: split the area in two along its longer axis at a
 * random 40-60% point, recurse into both halves, and carve one room
 * per leaf. Depth and minimum size bound the recursion.
 *
 * An axis may only be split if BOTH halves would still be at least
 * MIN_HALF across. Without that test the split point was clamped to
 * whatever fitted -- as little as 4 -- and a 4-wide leaf made
 * (a.w - 5) negative. Cast to uint32_t that wrapped to ~4 billion,
 * so the room came out with a garbage size, its centre landed a
 * billion tiles off the map, and the corridor carver then walked the
 * whole way there. That was the floor-2 freeze AND the missing
 * sanctum: the "farthest room" search always picked the garbage
 * room, so no shrine and no doors were ever placed. */
static void bsp_split(Gen *g, Rect a, int depth)
{
    const int ms = g->sp.min_split;
    int can_w = a.w >= ms && a.w >= 2 * MIN_HALF;
    int can_h = a.h >= ms && a.h >= 2 * MIN_HALF;
    int can_split = depth > 0 && (can_w || can_h)
                    && g->nrooms < MAX_ROOMS - 1;

    if (!can_split) {
        Rect r;
        /* The room fills 60-100% of the leaf, jittered inside it.
         * Both spans are floored at zero before becoming an unsigned
         * range, so a leaf smaller than the minimum room can never
         * wrap the subtraction. */
        int spanw = a.w - 5, spanh = a.h - 5;
        if (spanw < 0) spanw = 0;
        if (spanh < 0) spanh = 0;
        r.w = 4 + (int)rr(&g->rng, (uint32_t)spanw);
        r.h = 4 + (int)rr(&g->rng, (uint32_t)spanh);
        if (r.w > a.w - 1) r.w = a.w - 1;
        if (r.h > a.h - 1) r.h = a.h - 1;
        if (r.w < 1) r.w = 1;
        if (r.h < 1) r.h = 1;
        r.x = a.x + (int)rr(&g->rng, (uint32_t)(a.w - r.w));
        r.y = a.y + (int)rr(&g->rng, (uint32_t)(a.h - r.h));
        /* the caller's guard leaves room for one more, but pending
         * siblings could still overrun the array: refuse instead */
        if (g->nrooms >= MAX_ROOMS)
            return;
        g->rooms[g->nrooms++] = r;
        carve_room(g->m, r);
        return;
    }

    int vertical;
    if (can_w && can_h)
        vertical = a.w > a.h ? 1
                 : a.h > a.w ? 0
                 : (int)(rng_next(&g->rng) & 1);
    else
        vertical = can_w; /* only one axis has room to be split */

    if (vertical) {
        int sp = a.w * (40 + (int)rng_range(&g->rng, 21)) / 100;
        if (sp < MIN_HALF) sp = MIN_HALF;
        if (sp > a.w - MIN_HALF) sp = a.w - MIN_HALF;
        Rect l = { a.x, a.y, sp, a.h };
        Rect r = { a.x + sp, a.y, a.w - sp, a.h };
        bsp_split(g, l, depth - 1);
        bsp_split(g, r, depth - 1);
    } else {
        int sp = a.h * (40 + (int)rng_range(&g->rng, 21)) / 100;
        if (sp < MIN_HALF) sp = MIN_HALF;
        if (sp > a.h - MIN_HALF) sp = a.h - MIN_HALF;
        Rect t = { a.x, a.y, a.w, sp };
        Rect b = { a.x, a.y + sp, a.w, a.h - sp };
        bsp_split(g, t, depth - 1);
        bsp_split(g, b, depth - 1);
    }
}

/* L-shaped corridor between two points; hall styles carve it two
 * tiles wide, the maze style one. Consecutive BSP leaves are spatial
 * neighbours, so these stay short. */
static void carve_h(Gen *g, int x0, int x1, int y)
{
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) {
        carve(g->m, x, y);
        if (g->sp.wide_halls)
            carve(g->m, x, y + 1);
    }
}

static void carve_v(Gen *g, int y0, int y1, int x)
{
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) {
        carve(g->m, x, y);
        if (g->sp.wide_halls)
            carve(g->m, x + 1, y);
    }
}

static void corridor(Gen *g, Rect a, Rect b)
{
    int ax = a.x + a.w / 2, ay = a.y + a.h / 2;
    int bx = b.x + b.w / 2, by = b.y + b.h / 2;
    if (rng_next(&g->rng) & 1) {
        carve_h(g, ax, bx, ay);
        carve_v(g, ay, by, bx);
    } else {
        carve_v(g, ay, by, ax);
        carve_h(g, ax, bx, by);
    }
}

/* Mandapa pillars: a grid every 3 tiles in large rooms. A pillar is
 * only placed if the cell and its four neighbours are open floor, so
 * it can never seal a corridor mouth — and the flood fill below would
 * catch it even if the geometry conspired. */
static void place_pillars(Gen *g)
{
    for (int i = 0; i < g->nrooms; i++) {
        if (i == g->sanctum)
            continue; /* the sanctum stays an open chamber */
        Rect r = g->rooms[i];
        int step = g->sp.pillar_step;
        if (r.w < 7 || r.h < 7 || step <= 0)
            continue;
        for (int y = r.y + 2; y < r.y + r.h - 2; y += step) {
            for (int x = r.x + 2; x < r.x + r.w - 2; x += step) {
                Map *m = g->m;
                if (!map_solid(m, x, y)
                    && !map_solid(m, x - 1, y) && !map_solid(m, x + 1, y)
                    && !map_solid(m, x, y - 1) && !map_solid(m, x, y + 1))
                    m->tiles[y * m->w + x] = TILE_PILLAR;
            }
        }
    }
}

/* Recursive flood fill over WALKABLE tiles (deep water blocks feet).
 * Recursion depth is bounded by the open floor area, well within
 * stack limits at our map sizes. */
static int flood(const Map *m, uint8_t *mark, int x, int y)
{
    if (x < 0 || x >= m->w || y < 0 || y >= m->h)
        return 0;
    int i = y * m->w + x;
    if (mark[i] || !map_walkable(m, x, y))
        return 0;
    mark[i] = 1;
    return 1
        + flood(m, mark, x + 1, y) + flood(m, mark, x - 1, y)
        + flood(m, mark, x, y + 1) + flood(m, mark, x, y - 1);
}

static int connectivity_ok(const Map *m, int sx, int sy)
{
    uint8_t *mark = calloc((size_t)m->w * (size_t)m->h, 1);
    if (!mark)
        return 0;

    int total = 0;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (map_walkable(m, x, y))
                total++;

    int reached = flood(m, mark, sx, sy);
    free(mark);
    return reached == total;
}

static void add_spawn(Map *m, SpawnKind kind, double x, double y,
                      int arg)
{
    if (m->nspawns < MAX_SPAWNS) {
        m->spawns[m->nspawns].kind = kind;
        m->spawns[m->nspawns].x = x;
        m->spawns[m->nspawns].y = y;
        m->spawns[m->nspawns].arg = arg;
        m->nspawns++;
    }
}

/* Nearest walkable cell to a preferred spot (small spiral search),
 * for placing things where a pillar or water might sit. */
static int find_floor_near(const Map *m, int x, int y, int *ox, int *oy)
{
    for (int rad = 0; rad <= 3; rad++)
        for (int dy = -rad; dy <= rad; dy++)
            for (int dx = -rad; dx <= rad; dx++)
                if (map_walkable(m, x + dx, y + dy)) {
                    *ox = x + dx;
                    *oy = y + dy;
                    return 1;
                }
    return 0;
}

static void furnish(Gen *g)
{
    Map *m = g->m;

    for (int i = 1; i < g->nrooms; i++) {
        Rect r = g->rooms[i];
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        int gx, gy;

        if (i == g->sanctum) {
            /* the shrine at the heart; on the final floor the Asura
             * King alone holds it, otherwise the usual keepers */
            if (find_floor_near(m, cx, cy, &gx, &gy))
                add_spawn(m, SPAWN_SHRINE, gx + 0.5, gy + 0.5, 0);
            if (g->with_boss) {
                if (find_floor_near(m, cx + 2, cy + 1, &gx, &gy))
                    add_spawn(m, SPAWN_BOSS, gx + 0.5, gy + 0.5, 0);
            } else {
                if (find_floor_near(m, cx + 2, cy, &gx, &gy))
                    add_spawn(m, SPAWN_NAGA, gx + 0.5, gy + 0.5, 0);
                if (find_floor_near(m, cx - 2, cy, &gx, &gy))
                    add_spawn(m, SPAWN_DWARAPALAKA,
                              gx + 0.5, gy + 0.5, 0);
            }
            if (map_walkable(m, r.x + 1, r.y + 1))
                add_spawn(m, SPAWN_LAMP, r.x + 1.5, r.y + 1.5, 0);
            if (map_walkable(m, r.x + r.w - 2, r.y + r.h - 2))
                add_spawn(m, SPAWN_LAMP,
                          r.x + r.w - 1.5, r.y + r.h - 1.5, 0);
            continue;
        }

        /* larger rooms get a guardian, jittered off-centre. Grand
         * halls take a heavy dwarapalaka or an armoured champion;
         * mid rooms roll from the whole war-band. */
        int area = r.w * r.h;
        if (area >= 30 && find_floor_near(
                m, cx + (int)rr(&g->rng, 3) - 1,
                cy + (int)rr(&g->rng, 3) - 1, &gx, &gy)) {
            SpawnKind kind;
            if (area >= 60)
                kind = (rng_next(&g->rng) & 1) ? SPAWN_DWARAPALAKA
                                               : SPAWN_KNIGHT;
            else
                switch (rng_range(&g->rng, 4)) {
                case 0:  kind = SPAWN_YALI;   break;
                case 1:  kind = SPAWN_NAGA;   break;
                case 2:  kind = SPAWN_ARCHER; break;
                default: kind = SPAWN_KNIGHT; break;
                }
            add_spawn(m, kind, gx + 0.5, gy + 0.5, 0);
            /* archers like a second pair of eyes across the room */
            if (kind == SPAWN_ARCHER && area >= 48
                && find_floor_near(m, r.x + 2, r.y + r.h - 3,
                                   &gx, &gy))
                add_spawn(m, SPAWN_ARCHER, gx + 0.5, gy + 0.5, 0);
        }

        /* every room gets a lamp in one corner */
        if (map_walkable(m, r.x + 1, r.y + 1))
            add_spawn(m, SPAWN_LAMP, r.x + 1.5, r.y + 1.5, 0);
    }
}

/* Puzzle furniture: three order-marked lamps scattered through the
 * temple, up to three pillars converted to rotors, the tank sluice. */
static void place_puzzles(Gen *g, int tank)
{
    Map *m = g->m;
    int gx, gy;

    /* the three sequence lamps, in three different non-sanctum rooms */
    int placed = 0;
    for (int i = 1; i < g->nrooms && placed < 3; i++) {
        if (i == g->sanctum)
            continue;
        Rect r = g->rooms[i];
        if (find_floor_near(m, r.x + r.w - 2, r.y + 1, &gx, &gy)) {
            add_spawn(m, SPAWN_PUZZLE_LAMP, gx + 0.5, gy + 0.5,
                      ++placed);
        }
    }

    /* convert some pillars into rotors (skip if the floor has none) */
    m->nrotors = 0;
    for (int y = 1; y < m->h - 1 && m->nrotors < 3; y++)
        for (int x = 1; x < m->w - 1 && m->nrotors < 3; x++)
            if (m->tiles[y * m->w + x] == TILE_PILLAR
                && (rng_next(&g->rng) & 1)) {
                m->tiles[y * m->w + x] = TILE_ROTOR;
                m->meta[y * m->w + x] =
                    (uint8_t)rng_range(&g->rng, 4);
                m->nrotors++;
            }
    /* a pre-solved alignment would be a non-puzzle: desync one */
    if (m->nrotors >= 2) {
        int first = -1, all_same = 1;
        for (int i = 0; i < m->w * m->h; i++)
            if (m->tiles[i] == TILE_ROTOR) {
                if (first < 0)
                    first = i;
                else if (m->meta[i] != m->meta[first])
                    all_same = 0;
            }
        if (all_same && first >= 0)
            m->meta[first] = (uint8_t)((m->meta[first] + 1) & 3);
    }

    /* sluice lever on the tank rim */
    if (tank >= 0) {
        Rect r = g->rooms[tank];
        if (find_floor_near(m, r.x + 1, r.y + r.h / 2, &gx, &gy))
            add_spawn(m, SPAWN_LEVER, gx + 0.5, gy + 0.5, 0);
    }

    /* 2-3 treasure chests in rooms picked off the tail of the room
     * list (the tail is far from the spawn room in BSP order) */
    int chests = 2 + (int)rng_range(&g->rng, 2);
    for (int i = g->nrooms - 1; i >= 1 && chests > 0; i -= 2) {
        if (i == g->sanctum)
            continue;
        Rect r = g->rooms[i];
        if (find_floor_near(m, r.x + 1, r.y + r.h - 2, &gx, &gy)) {
            add_spawn(m, SPAWN_CHEST, gx + 0.5, gy + 0.5, 0);
            chests--;
        }
    }
}

static void build(Gen *g, uint32_t seed)
{
    Map *m = g->m;

    memset(m->tiles, TILE_GRANITE, (size_t)m->w * (size_t)m->h);
    memset(m->floors, FLOOR_STONE, (size_t)m->w * (size_t)m->h);
    memset(m->meta, 0, (size_t)m->w * (size_t)m->h);
    m->nspawns = 0;
    m->nrotors = 0;
    m->tank_x = m->tank_y = m->tank_w = m->tank_h = 0;
    /* cleared too, so a retry can never inherit the last attempt's
     * sanctum rectangle if this one fails to choose a room */
    m->sanctum_x = m->sanctum_y = m->sanctum_w = m->sanctum_h = 0;
    g->nrooms = 0;
    g->sanctum = -1;
    rng_seed(&g->rng, seed);

    Rect all = { 1, 1, m->w - 2, m->h - 2 };
    /* per-style recursion depth; this used to be hard-coded to 5, so
     * the depth column of the style table was silently ignored and
     * all three floors split identically */
    bsp_split(g, all, g->sp.depth);

    for (int i = 0; i + 1 < g->nrooms; i++)
        corridor(g, g->rooms[i], g->rooms[i + 1]);

    /* the sanctum is the room farthest from where the player starts */
    Rect r0 = g->rooms[0];
    double best = -1.0;
    for (int i = 1; i < g->nrooms; i++) {
        double dx = (g->rooms[i].x + g->rooms[i].w / 2.0)
                  - (r0.x + r0.w / 2.0);
        double dy = (g->rooms[i].y + g->rooms[i].h / 2.0)
                  - (r0.y + r0.h / 2.0);
        if (dx * dx + dy * dy > best) {
            best = dx * dx + dy * dy;
            g->sanctum = i;
        }
    }

    /* dress the sanctum: every wall touching its interior becomes
     * carved sanctum stone, so the room announces itself */
    if (g->sanctum >= 0) {
        Rect s = g->rooms[g->sanctum];
        for (int y = s.y - 1; y <= s.y + s.h; y++)
            for (int x = s.x - 1; x <= s.x + s.w; x++)
                if (map_tile(m, x, y) == TILE_GRANITE
                    && x > 0 && y > 0 && x < m->w - 1 && y < m->h - 1)
                    m->tiles[y * m->w + x] = TILE_SANCTUM;
    }

    place_pillars(g);

    /* temple tank: the biggest leftover room gets a sunken pool */
    int tank = -1, tank_area = 0;
    for (int i = 1; i < g->nrooms; i++) {
        Rect r = g->rooms[i];
        if (i != g->sanctum && r.w >= 8 && r.h >= 6
            && r.w * r.h > tank_area) {
            tank = i;
            tank_area = r.w * r.h;
        }
    }
    if (tank >= 0) {
        Rect r = g->rooms[tank];
        for (int y = r.y + 2; y < r.y + r.h - 2; y++)
            for (int x = r.x + 2; x < r.x + r.w - 2; x++)
                if (!map_solid(m, x, y))
                    m->floors[y * m->w + x] = FLOOR_WATER;
        m->tank_x = r.x;
        m->tank_y = r.y;
        m->tank_w = r.w;
        m->tank_h = r.h;
    }

    if (g->sanctum >= 0) {
        Rect s = g->rooms[g->sanctum];
        m->sanctum_x = s.x;
        m->sanctum_y = s.y;
        m->sanctum_w = s.w;
        m->sanctum_h = s.h;
    }

    /* player spawn: centre of room 0, facing the next room */
    m->spawn_x = r0.x + r0.w / 2.0 + 0.5;
    m->spawn_y = r0.y + r0.h / 2.0 + 0.5;
    if (g->nrooms > 1) {
        Rect r1 = g->rooms[1];
        m->spawn_angle = atan2(
            (r1.y + r1.h / 2.0) - m->spawn_y,
            (r1.x + r1.w / 2.0) - m->spawn_x);
    } else {
        m->spawn_angle = 0.0;
    }

    furnish(g);
    place_puzzles(g, tank);
}

/* Seal every entrance of the sanctum with door tiles. Runs AFTER the
 * connectivity proof (doors would fail it by design — that's their
 * job); the puzzles open them at runtime. */
static void seal_sanctum(Map *m)
{
    int x0 = m->sanctum_x - 1, y0 = m->sanctum_y - 1;
    int x1 = m->sanctum_x + m->sanctum_w;
    int y1 = m->sanctum_y + m->sanctum_h;

    for (int x = x0; x <= x1; x++) {
        if (map_tile(m, x, y0) == TILE_FLOOR)
            m->tiles[y0 * m->w + x] = TILE_DOOR;
        if (map_tile(m, x, y1) == TILE_FLOOR)
            m->tiles[y1 * m->w + x] = TILE_DOOR;
    }
    for (int y = y0; y <= y1; y++) {
        if (map_tile(m, x0, y) == TILE_FLOOR)
            m->tiles[y * m->w + x0] = TILE_DOOR;
        if (map_tile(m, x1, y) == TILE_FLOOR)
            m->tiles[y * m->w + x1] = TILE_DOOR;
    }
}

int map_generate(Map *m, int w, int h, uint32_t seed,
                 MapStyle style, int with_boss)
{
    m->w = w;
    m->h = h;
    m->tiles = malloc((size_t)w * (size_t)h);
    m->floors = malloc((size_t)w * (size_t)h);
    m->meta = malloc((size_t)w * (size_t)h);
    /* +7 rounds up so the last partial byte of bits exists */
    m->seen = calloc(((size_t)w * (size_t)h + 7) / 8, 1);
    if (!m->tiles || !m->floors || !m->meta || !m->seen) {
        map_free(m);
        return -1;
    }

    Gen g;
    g.m = m;
    g.sp = styles[style];
    g.with_boss = with_boss;

    /* A pillar sealing off a pocket is geometrically possible, so we
     * PROVE connectivity by flood fill and reroll the seed until the
     * proof passes. In practice the first attempt almost always does. */
    for (int attempt = 0; attempt < 16; attempt++) {
        build(&g, seed + (uint32_t)attempt * 0x9E3779B9u);
        if (connectivity_ok(m, (int)m->spawn_x, (int)m->spawn_y)) {
            seal_sanctum(m);
            return 0;
        }
    }

    /* unreachable in practice; fail loudly rather than ship a maze
     * with sealed rooms */
    map_free(m);
    return -1;
}

void map_free(Map *m)
{
    free(m->tiles);
    free(m->floors);
    free(m->meta);
    free(m->seen);
    m->tiles = NULL;
    m->floors = NULL;
    m->meta = NULL;
    m->seen = NULL;
    m->w = m->h = 0;
}
