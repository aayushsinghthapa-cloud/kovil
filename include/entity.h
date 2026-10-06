#ifndef KOVIL_ENTITY_H
#define KOVIL_ENTITY_H

#include <stdint.h>

#include "map.h"
#include "player.h"

typedef enum {
    ENT_DWARAPALAKA, /* door-guardian statue: slow, heavy, relentless */
    ENT_YALI,        /* leonine temple beast: fast, fragile, lunging */
    ENT_NAGA,        /* serpent guardian: far sight, quick strikes */
    ENT_LAMP,        /* brass oil lamp (scenery + light) */
    ENT_FLAME,       /* the player's thrown consecrated flame */
    ENT_PUZZLE_LAMP, /* order-marked lamp; light them in pip order */
    ENT_LEVER,       /* tank sluice: raises the causeway */
    ENT_SHRINE,      /* the sanctum's great lamp; light it to descend */
    ENT_BOSS,        /* the Asura King, floor 3's final guardian */
    ENT_BOSSFIRE,    /* his venom-fire volley */
    ENT_ARCHER,      /* asura bowman: keeps distance, looses arrows */
    ENT_KNIGHT,      /* asura champion: armoured, heavy blade */
    ENT_ARROW,       /* an arrow in flight (either side's) */
    ENT_ITEM,        /* a dropped item waiting to be picked up */
    ENT_CHEST        /* treasure chest; E opens it once */
} EntityType;

/* is this entity a combat enemy? (shared filter used all over) */
#define ENTITY_IS_ENEMY(e) \
    ((e)->type == ENT_DWARAPALAKA || (e)->type == ENT_YALI \
     || (e)->type == ENT_NAGA || (e)->type == ENT_BOSS \
     || (e)->type == ENT_ARCHER || (e)->type == ENT_KNIGHT)

typedef enum {
    ST_DORMANT,   /* stone-still; indistinguishable from decoration */
    ST_ALERTED,   /* saw the player; brief waking pause */
    ST_PURSUING,  /* pathfinding toward the player */
    ST_ATTACKING, /* in range: wind-up, strike, recover */
    ST_DYING      /* short crumble timer, then removed */
} EntityState;

typedef struct Entity Entity;
typedef struct EntityList EntityList;

/* View of the world handed to every think function so behaviours can
 * see the map, hurt the player, and (for projectiles) hit other
 * entities — all without global variables. */
typedef struct {
    const Map *map;
    Player *player;
    EntityList *ents;
    double dt;
} World;

struct Entity {
    EntityType type;
    EntityState state;
    double x, y;
    int hp;
    int maxhp;    /* for the floating health bar above the head */
    int tex;      /* TextureId of the current sprite image */
    uint8_t glow; /* minimum brightness: flames ignore darkness */
    int remove;   /* set by think; swept after the update pass */

    /* Minecraft-mob feel, shared by everything that can be hit: */
    double hurt_t;     /* red-flash seconds remaining */
    double kbx, kby;   /* knockback velocity, decays quickly */
    double height;     /* world height in wall units; 0 means 1.0 */
    double widthm;     /* sprite width multiplier; 0 means 1.0 */
    double combat_time; /* seconds since waking; feeds adaptation */
    double anim;       /* walk-cycle phase, advanced by DISTANCE
                        * travelled, so gaits stop when the body does */

    /* Per-type behaviour as a function pointer: the update loop calls
     * e->think(e, w) with no idea what kind of entity it has. */
    void (*think)(Entity *e, World *w);

    /* Variant data: only ONE member is live at a time, chosen by
     * 'type'. Enemies share one member; a lamp or a projectile needs
     * entirely different fields in the same bytes. */
    union {
        struct {
            double wake_range;  /* how far the statue can see */
            double speed;       /* tiles per second */
            int strike_damage;
            double timer;       /* current state's countdown */
            double repath;      /* seconds until the next A* query */
            double wx, wy;      /* current waypoint centre */
            int has_waypoint;
            double special;  /* ability cooldown (volley, pounce...) */
            double special2; /* ability duration / secondary timer */
            int shots;       /* archer: arrows since the last volley */
        } enemy;
        struct {
            double phase; /* flicker oscillator, radians */
        } lamp;
        struct {
            double vx, vy; /* velocity, tiles per second */
            double life;   /* seconds until burnout */
            int hostile;   /* arrows: 1 = hurts the player */
        } flame;
        struct {
            int what; /* ItemType waiting on the ground */
        } item;
        struct {
            int opened;
        } chest;
        struct {
            int order; /* 1..3: position in the lighting sequence */
            int lit;
        } plamp;
        struct {
            int on;
        } lever;
        struct {
            int lit;
        } shrine;
    } as;
};

struct EntityList {
    Entity *items; /* owned; grows by realloc doubling */
    int count;
    int cap;
};

int entlist_init(EntityList *el);
void entlist_free(EntityList *el);
/* Returns pointer to the stored copy, or NULL on allocation failure.
 * Only valid until the next add (realloc may move the array). */
Entity *entlist_add(EntityList *el, Entity proto);

/* Runs every think function, then sweeps out removed entities. */
void entities_update(EntityList *el, World *w);

/* Damage an enemy (waking it if dormant), with a knockback impulse
 * along (kx,ky). No-op on non-enemies. el receives loot drops when
 * the blow kills (pass NULL to skip drops). */
void entity_damage(Entity *e, EntityList *el, int amount,
                   double kx, double ky);

Entity entity_make_dwarapalaka(double x, double y);
Entity entity_make_yali(double x, double y);
Entity entity_make_naga(double x, double y);
Entity entity_make_lamp(double x, double y);
Entity entity_make_flame(double x, double y, double dirx, double diry);
Entity entity_make_puzzle_lamp(double x, double y, int order);
Entity entity_make_lever(double x, double y);
Entity entity_make_shrine(double x, double y);
Entity entity_make_boss(double x, double y);
Entity entity_make_archer(double x, double y);
Entity entity_make_knight(double x, double y);
Entity entity_make_arrow(double x, double y, double dirx, double diry,
                         int hostile);
Entity entity_make_item(double x, double y, int item_type);
Entity entity_make_chest(double x, double y);

/* ---- adaptation ------------------------------------------------------
 * The temple "learns": each enemy type tracks how many of its kin
 * have fallen and how quickly. Fast kills breed tougher, quicker
 * spawns of that type; slow struggles ease off. A plain heuristic,
 * persisted through the save file. */
#define ADAPT_TYPES 8
void adapt_reset(void);
double adapt_mult(EntityType t); /* hp/speed multiplier, ~0.85..1.6 */
/* blob for save/load: [kills, avg-deciseconds] per type */
void adapt_export(int32_t out[ADAPT_TYPES * 2]);
void adapt_import(const int32_t in[ADAPT_TYPES * 2]);

#endif
