#include "puzzle.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "audio.h"
#include "texture.h"

#define REACH 1.6  /* how far the player's hands reach */
#define FACING 0.6 /* cos of the interaction cone half-angle */

void puzzle_init(Puzzle *pz, const Map *m)
{
    pz->lamps_total = 0;
    for (int i = 0; i < m->nspawns; i++)
        if (m->spawns[i].kind == SPAWN_PUZZLE_LAMP)
            pz->lamps_total++;
    pz->lamps_next = 1;
    pz->lamps_done = (pz->lamps_total == 0);
    pz->rotors_done = (m->nrotors == 0);
    pz->doors_open = 0;
    pz->shrine_lit = 0;
}

/* both puzzles down => grind the sanctum doors open */
static void check_doors(Puzzle *pz, Map *m)
{
    if (pz->doors_open || !pz->lamps_done || !pz->rotors_done)
        return;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (m->tiles[y * m->w + x] == TILE_DOOR)
                m->tiles[y * m->w + x] = TILE_FLOOR;
    pz->doors_open = 1;
    audio_play(SND_GRIND);
}

static void snuff_all(Puzzle *pz, EntityList *el)
{
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->items[i];
        if (e->type == ENT_PUZZLE_LAMP && e->as.plamp.lit) {
            e->as.plamp.lit = 0;
            e->tex = TEX_PLAMP_1 + (e->as.plamp.order - 1);
        }
    }
    pz->lamps_next = 1;
    audio_play(SND_PLAYER_HURT); /* a dull, wrong-feeling thud */
}

static int try_lamp(Puzzle *pz, Map *m, EntityList *el, Entity *e)
{
    if (e->as.plamp.lit)
        return 0;
    if (e->as.plamp.order != pz->lamps_next) {
        snuff_all(pz, el);
        return 1;
    }
    e->as.plamp.lit = 1;
    e->tex = TEX_LAMP; /* it now burns like any lamp */
    audio_play(SND_FLAME);
    pz->lamps_next++;
    if (pz->lamps_next > pz->lamps_total) {
        pz->lamps_done = 1;
        audio_play(SND_CHIME);
        check_doors(pz, m);
    }
    return 1;
}

static int try_lever(Puzzle *pz, Map *m, Entity *e)
{
    (void)pz;
    e->as.lever.on = !e->as.lever.on;
    e->tex = e->as.lever.on ? TEX_LEVER_ON : TEX_LEVER_OFF;

    /* raise/lower a stone causeway across the tank's middle row */
    int row = m->tank_y + m->tank_h / 2;
    for (int x = m->tank_x; x < m->tank_x + m->tank_w; x++) {
        int i = row * m->w + x;
        if (map_tile(m, x, row) != TILE_FLOOR)
            continue;
        if (e->as.lever.on) {
            if (m->floors[i] == FLOOR_WATER)
                m->floors[i] = FLOOR_STONE;
        } else {
            /* only re-flood inside the tank basin proper */
            if (x >= m->tank_x + 2 && x < m->tank_x + m->tank_w - 2
                && row >= m->tank_y + 2
                && row < m->tank_y + m->tank_h - 2)
                m->floors[i] = FLOOR_WATER;
        }
    }
    audio_play(SND_GRIND);
    return 1;
}

static int try_shrine(Puzzle *pz, EntityList *el, Entity *e)
{
    if (e->as.shrine.lit)
        return 0;
    /* while the Asura King lives, the shrine will not take flame */
    for (int i = 0; i < el->count; i++)
        if (el->items[i].type == ENT_BOSS
            && el->items[i].state != ST_DYING) {
            audio_play(SND_PLAYER_HURT);
            return 1;
        }
    e->as.shrine.lit = 1;
    e->tex = TEX_SHRINE_ON;
    e->glow = 255;
    pz->shrine_lit = 1; /* main() sees this and descends a floor */
    audio_play(SND_CHIME);
    return 1;
}

/* all rotor arrows agreeing = solved */
static void check_rotors(Puzzle *pz, Map *m)
{
    int first = -1;
    for (int i = 0; i < m->w * m->h; i++) {
        if (m->tiles[i] != TILE_ROTOR)
            continue;
        if (first < 0)
            first = i;
        else if (m->meta[i] != m->meta[first])
            return;
    }
    if (!pz->rotors_done) {
        pz->rotors_done = 1;
        audio_play(SND_CHIME);
        check_doors(pz, m);
    }
}

/* the entity (lamp/lever/shrine/chest) the player is facing, if any */
static Entity *facing_entity(const EntityList *el, const Player *p)
{
    Entity *best = NULL;
    double best_d = REACH;
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->items[i];
        if (e->type != ENT_PUZZLE_LAMP && e->type != ENT_LEVER
            && e->type != ENT_SHRINE && e->type != ENT_CHEST)
            continue;
        double dx = e->x - p->x, dy = e->y - p->y;
        double d = sqrt(dx * dx + dy * dy);
        if (d < best_d && d > 1e-6
            && (dx * p->dirX + dy * p->dirY) / d > FACING) {
            best = e;
            best_d = d;
        }
    }
    return best;
}

/* the rotor tile the player is facing, or -1 */
static int facing_rotor(const Map *m, const Player *p)
{
    for (int step = 1; step <= 2; step++) {
        int tx = (int)(p->x + p->dirX * 0.75 * step);
        int ty = (int)(p->y + p->dirY * 0.75 * step);
        if (map_tile(m, tx, ty) == TILE_ROTOR)
            return ty * m->w + tx;
        if (map_solid(m, tx, ty))
            break; /* a wall blocks the reach */
    }
    return -1;
}

const char *puzzle_prompt(const Puzzle *pz, const Map *m,
                          const EntityList *el, const Player *p)
{
    static char buf[48];
    Entity *e = facing_entity(el, p);
    if (e) {
        switch (e->type) {
        case ENT_PUZZLE_LAMP:
            if (e->as.plamp.lit)
                return NULL;
            snprintf(buf, sizeof buf,
                     "E: LIGHT LAMP %d - NEXT NEEDED: %d",
                     e->as.plamp.order, pz->lamps_next);
            return buf;
        case ENT_LEVER:
            return e->as.lever.on ? "E: LOWER THE CAUSEWAY"
                                  : "E: RAISE THE CAUSEWAY";
        case ENT_SHRINE:
            return e->as.shrine.lit ? NULL : "E: LIGHT THE SHRINE";
        case ENT_CHEST:
            return e->as.chest.opened ? NULL : "E: OPEN CHEST";
        default:
            break;
        }
    }
    if (facing_rotor(m, p) >= 0)
        return pz->rotors_done ? NULL
             : "E: TURN PILLAR - MAKE ALL ARROWS AGREE";
    return NULL;
}

int puzzle_interact(Puzzle *pz, Map *m, EntityList *el, const Player *p)
{
    Entity *best = facing_entity(el, p);
    if (best) {
        switch (best->type) {
        case ENT_PUZZLE_LAMP: return try_lamp(pz, m, el, best);
        case ENT_LEVER:       return try_lever(pz, m, best);
        case ENT_SHRINE:      return try_shrine(pz, el, best);
        default:              break; /* chests are main()'s business:
                                      * opening one needs the
                                      * inventory */
        }
    }

    int ri = facing_rotor(m, p);
    if (ri >= 0) {
        m->meta[ri] = (uint8_t)((m->meta[ri] + 1) & 3);
        audio_play(SND_GRIND);
        check_rotors(pz, m);
        return 1;
    }
    return 0;
}
