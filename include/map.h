#ifndef KOVIL_MAP_H
#define KOVIL_MAP_H

#include <stdint.h>

typedef enum {
    TILE_FLOOR   = 0,
    TILE_GRANITE = 1, /* stone brick temple wall */
    TILE_PILLAR  = 2, /* chiseled mandapa pillar */
    TILE_SANCTUM = 3, /* inner sanctum brick */
    TILE_DOOR    = 4, /* sealed sanctum door: opens via puzzles */
    TILE_ROTOR   = 5  /* rotatable pillar; orientation in meta layer */
} TileType;

/* Second grid layer: what the ground itself is made of. */
typedef enum {
    FLOOR_STONE = 0,
    FLOOR_WATER = 1 /* the temple tank */
} FloorType;

/* Where the generator wants things placed. The map module doesn't
 * know what an Entity is (that would be a circular dependency);
 * main() converts these into real entities. */
typedef enum {
    SPAWN_DWARAPALAKA,
    SPAWN_YALI,
    SPAWN_NAGA,
    SPAWN_LAMP,
    SPAWN_PUZZLE_LAMP, /* arg = its position in the lighting order */
    SPAWN_LEVER,       /* the tank sluice */
    SPAWN_SHRINE,      /* the sanctum's great lamp: light it to win */
    SPAWN_BOSS,        /* the Asura King (final floor's sanctum) */
    SPAWN_ARCHER,      /* asura bowman: kites and shoots */
    SPAWN_KNIGHT,      /* asura champion: armoured, heavy blade */
    SPAWN_CHEST        /* treasure chest holding a potion */
} SpawnKind;

/* generation styles: same BSP engine, different architecture */
typedef enum {
    STYLE_HALLS = 0, /* broad rooms, pillared mandapa halls */
    STYLE_MAZE,      /* many small rooms, narrow corridors */
    STYLE_CITADEL    /* few huge chambers, dense pillars */
} MapStyle;

typedef struct {
    SpawnKind kind;
    double x, y;
    int arg;
} Spawn;

#define MAX_SPAWNS 128

typedef struct {
    int w, h;
    uint8_t *tiles;  /* w*h wall layer, row-major; owned */
    uint8_t *floors; /* w*h floor layer; owned */
    uint8_t *meta;   /* w*h per-tile extra (rotor orientation); owned */
    uint8_t *seen;   /* fog-of-war BITSET: one bit per tile, packed
                      * eight to a byte; owned. The minimap only draws
                      * tiles whose bit is set. */

    double spawn_x, spawn_y, spawn_angle; /* player start */
    Spawn spawns[MAX_SPAWNS];
    int nspawns;

    /* room rectangles the puzzles care about */
    int sanctum_x, sanctum_y, sanctum_w, sanctum_h;
    int tank_x, tank_y, tank_w, tank_h; /* all 0 if no tank */
    int nrotors; /* rotatable pillars placed this floor */
} Map;

/* Generate a temple floor by recursive BSP splitting. The same seed
 * always produces the same temple. with_boss puts the Asura King in
 * the sanctum in place of the usual keepers. Returns 0 on success,
 * -1 on allocation failure. */
int map_generate(Map *m, int w, int h, uint32_t seed,
                 MapStyle style, int with_boss);
void map_free(Map *m);

/* Anything outside the grid counts as solid. */
int map_solid(const Map *m, int x, int y);
uint8_t map_tile(const Map *m, int x, int y);
uint8_t map_floor(const Map *m, int x, int y);
uint8_t map_meta(const Map *m, int x, int y);

/* Walkable = open floor that isn't deep tank water. Bodies use this;
 * sight (map_los) and projectiles only care about map_solid. */
int map_walkable(const Map *m, int x, int y);

/* Fog of war: mark every tile within radius of (x,y) as seen, and
 * query one tile's bit. */
void map_reveal(Map *m, double x, double y, double radius);
int map_seen(const Map *m, int x, int y);

/* 1 if a straight line between the two points crosses no solid tile.
 * Same DDA grid walk as the renderer, reused for enemy vision. */
int map_los(const Map *m, double x0, double y0, double x1, double y1);

#endif
