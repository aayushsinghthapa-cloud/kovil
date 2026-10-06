#include "entity.h"

#include <math.h>
#include <stdlib.h>

#include "audio.h"
#include "particles.h"
#include "pathfind.h"
#include "texture.h"
#include "util.h"

#define ENEMY_RADIUS 0.28

static void enemy_move(Entity *e, const Map *m, double dx, double dy);

/* ---- adaptation ------------------------------------------------------
 * Per-type combat memory. mult grows with each fallen kinsman and
 * grows faster when they died quickly — and drops below 1 when the
 * player is struggling (slow kills). Clamped so the game stays fair. */
typedef struct {
    int kills;
    double total_time; /* summed seconds from waking to death */
} AdaptSlot;

static AdaptSlot adapt[ADAPT_TYPES];

/* enemy types are scattered through the enum; give each a slot */
static int adapt_index(EntityType t)
{
    switch (t) {
    case ENT_DWARAPALAKA: return 0;
    case ENT_YALI:        return 1;
    case ENT_NAGA:        return 2;
    case ENT_BOSS:        return 3;
    case ENT_ARCHER:      return 4;
    case ENT_KNIGHT:      return 5;
    default:              return -1;
    }
}

void adapt_reset(void)
{
    for (int i = 0; i < ADAPT_TYPES; i++)
        adapt[i] = (AdaptSlot){ 0, 0.0 };
}

double adapt_mult(EntityType t)
{
    int idx = adapt_index(t);
    if (idx < 0)
        return 1.0;
    const AdaptSlot *a = &adapt[idx];
    if (a->kills == 0)
        return 1.0;
    double m = 1.0 + (a->kills > 10 ? 10 : a->kills) * 0.05;
    double avg = a->total_time / a->kills;
    if (avg < 5.0)
        m += 0.15;      /* slaughtered fast: they come back harder */
    else if (avg > 20.0)
        m -= 0.25;      /* long struggles: the temple relents */
    if (m < 0.85) m = 0.85;
    if (m > 1.6)  m = 1.6;
    return m;
}

static void adapt_record(EntityType t, double time)
{
    int idx = adapt_index(t);
    if (idx >= 0) {
        adapt[idx].kills++;
        adapt[idx].total_time += time;
    }
}

void adapt_export(int32_t out[ADAPT_TYPES * 2])
{
    for (int i = 0; i < ADAPT_TYPES; i++) {
        out[i * 2] = adapt[i].kills;
        out[i * 2 + 1] = (int32_t)(adapt[i].total_time * 10.0);
    }
}

void adapt_import(const int32_t in[ADAPT_TYPES * 2])
{
    for (int i = 0; i < ADAPT_TYPES; i++) {
        adapt[i].kills = in[i * 2];
        adapt[i].total_time = in[i * 2 + 1] / 10.0;
    }
}
#define ATTACK_REACH 1.25  /* how close before switching to attack */
#define WINDUP 0.35        /* seconds of attack telegraph */
#define RECOVER 0.75       /* seconds after a strike */
#define WAKE_PAUSE 0.7     /* seconds of waking shudder */
#define DIE_TIME 0.6

/* ---- growable array -------------------------------------------------- */

int entlist_init(EntityList *el)
{
    el->count = 0;
    el->cap = 8;
    el->items = malloc((size_t)el->cap * sizeof *el->items);
    return el->items ? 0 : -1;
}

void entlist_free(EntityList *el)
{
    free(el->items);
    el->items = NULL;
    el->count = el->cap = 0;
}

Entity *entlist_add(EntityList *el, Entity proto)
{
    if (el->count == el->cap) {
        /* Doubling keeps the AMORTISED cost of adds constant: n adds
         * cause at most ~2n element moves in total. realloc into a
         * temporary so the old block isn't leaked if it fails. */
        int new_cap = el->cap * 2;
        Entity *grown =
            realloc(el->items, (size_t)new_cap * sizeof *grown);
        if (!grown)
            return NULL;
        el->items = grown;
        el->cap = new_cap;
    }
    el->items[el->count] = proto;
    return &el->items[el->count++];
}

/* Make sure at least n more entities fit WITHOUT moving the array.
 * Think functions hold a pointer into it while they spawn projectiles;
 * growing mid-think would leave that pointer dangling. */
static int entlist_reserve(EntityList *el, int n)
{
    if (el->count + n <= el->cap)
        return 0;
    int new_cap = el->cap * 2;
    while (new_cap < el->count + n)
        new_cap *= 2;
    Entity *grown = realloc(el->items, (size_t)new_cap * sizeof *grown);
    if (!grown)
        return -1;
    el->items = grown;
    el->cap = new_cap;
    return 0;
}

void entities_update(EntityList *el, World *w)
{
    for (int i = 0; i < el->count; i++) {
        /* headroom for anything this think spawns (a volley is 3) */
        entlist_reserve(el, 4);
        Entity *e = &el->items[i];
        double ox = e->x, oy = e->y;
        if (e->hurt_t > 0.0)
            e->hurt_t -= w->dt;
        /* knockback: applied to every entity with an impulse, with a
         * sharp exponential decay — the Minecraft "pop" backwards */
        if (e->kbx != 0.0 || e->kby != 0.0) {
            enemy_move(e, w->map, e->kbx * w->dt, e->kby * w->dt);
            double decay = 1.0 - 6.0 * w->dt;
            if (decay < 0.0)
                decay = 0.0;
            e->kbx *= decay;
            e->kby *= decay;
        }
        if (e->think)
            e->think(e, w); /* dynamic dispatch, C style */
        /* gait: the walk cycle advances with distance covered, not
         * with time — a body that stands still stops mid-stride
         * instead of jogging on the spot */
        double mdx = e->x - ox, mdy = e->y - oy;
        e->anim += sqrt(mdx * mdx + mdy * mdy) * 7.0;
    }
    /* sweep: swap-remove keeps this O(n) with no memory shuffling */
    for (int i = 0; i < el->count;) {
        if (el->items[i].remove)
            el->items[i] = el->items[--el->count];
        else
            i++;
    }
}

/* ---- shared enemy machinery ----------------------------------------- */

static int body_blocked(const Map *m, double x, double y)
{
    return !map_walkable(m, (int)(x - ENEMY_RADIUS), (int)(y - ENEMY_RADIUS))
        || !map_walkable(m, (int)(x + ENEMY_RADIUS), (int)(y - ENEMY_RADIUS))
        || !map_walkable(m, (int)(x - ENEMY_RADIUS), (int)(y + ENEMY_RADIUS))
        || !map_walkable(m, (int)(x + ENEMY_RADIUS), (int)(y + ENEMY_RADIUS));
}

/* axis-separated, same idea as the player: slides along walls */
static void enemy_move(Entity *e, const Map *m, double dx, double dy)
{
    if (!body_blocked(m, e->x + dx, e->y))
        e->x += dx;
    if (!body_blocked(m, e->x, e->y + dy))
        e->y += dy;
}

static double dist_to_player(const Entity *e, const World *w)
{
    double dx = w->player->x - e->x, dy = w->player->y - e->y;
    return sqrt(dx * dx + dy * dy);
}

/* pan/gain for a sound made at world position (x,y), heard by the
 * player: right-vector projection gives pan, distance gives gain */
static void play_at(const World *w, double x, double y, SoundId id)
{
    double dx = x - w->player->x, dy = y - w->player->y;
    double d = sqrt(dx * dx + dy * dy);
    float gain = (float)(1.0 / (1.0 + d * 0.18));
    float pan = 0.5f;
    if (d > 0.3) {
        double rightness = (dx * w->player->planeX
                            + dy * w->player->planeY) / (0.66 * d);
        pan = 0.5f + 0.4f * (float)rightness;
    }
    audio_play_at(id, pan, gain);
}

/* awake sprite is always dormant sprite + 1 in the TextureId enum */
static void wake(Entity *e, const World *w)
{
    if (e->state == ST_DORMANT) {
        e->state = ST_ALERTED;
        e->as.enemy.timer = WAKE_PAUSE;
        e->tex += 1;
        e->glow = 96;
        if (w)
            play_at(w, e->x, e->y, SND_ROAR);
        else
            audio_play(SND_ROAR);
    }
}

static Rng loot_rng = { 0x100D1234u };

/* what falls when this enemy dies. The bow and sword drop reliably —
 * they are the archer's and champion's whole point. */
static void drop_loot(const Entity *e, EntityList *el)
{
    if (!el)
        return;
    double x = e->x, y = e->y;
    switch (e->type) {
    case ENT_ARCHER:
        entlist_add(el, entity_make_item(x, y, ITEM_BOW));
        break;
    case ENT_KNIGHT:
        entlist_add(el, entity_make_item(x, y, ITEM_SWORD));
        if (rng_range(&loot_rng, 100) < 40)
            entlist_add(el, entity_make_item(x + 0.4, y, ITEM_SHIELD));
        break;
    case ENT_DWARAPALAKA:
        if (rng_range(&loot_rng, 100) < 30)
            entlist_add(el, entity_make_item(x, y, ITEM_SHIELD));
        break;
    case ENT_YALI:
        if (rng_range(&loot_rng, 100) < 30)
            entlist_add(el, entity_make_item(x, y, ITEM_POT_HEAL));
        break;
    case ENT_NAGA:
        if (rng_range(&loot_rng, 100) < 30)
            entlist_add(el, entity_make_item(x, y, ITEM_POT_SPD));
        break;
    case ENT_BOSS:
        /* the king yields a full panoply */
        entlist_add(el, entity_make_item(x - 0.5, y, ITEM_SWORD));
        entlist_add(el, entity_make_item(x + 0.5, y, ITEM_SHIELD));
        entlist_add(el, entity_make_item(x, y + 0.5, ITEM_POT_HEAL));
        break;
    default:
        break;
    }
}

void entity_damage(Entity *e, EntityList *el, int amount,
                   double kx, double ky)
{
    if (!ENTITY_IS_ENEMY(e) || e->state == ST_DYING)
        return;
    /* the champion's round shield turns one blow in three, at random */
    if (e->type == ENT_KNIGHT && e->state != ST_DORMANT
        && rng_range(&loot_rng, 3) == 0) {
        audio_play(SND_HIT_STONE);
        particles_burst(e->x, e->y, 0.5, 8, COLOR_RGB(255, 240, 150));
        return;
    }
    /* a dwarapalaka in stone form is, literally, a rock */
    if (e->type == ENT_DWARAPALAKA && e->as.enemy.special2 > 0.0) {
        audio_play(SND_HIT_STONE);
        particles_burst(e->x, e->y, 0.4, 4, COLOR_RGB(178, 170, 152));
        return;
    }
    wake(e, NULL); /* being struck wakes a statue that never saw you */
    e->hp -= amount;
    e->hurt_t = 0.25; /* red flash, Minecraft-style */
    /* knockback pop — armour and sheer mass resist it */
    double kb = e->type == ENT_BOSS ? 0.8
              : e->type == ENT_KNIGHT ? 1.4 : 3.2;
    e->kbx += kx * kb;
    e->kby += ky * kb;
    audio_play(SND_HIT_STONE);
    particles_burst(e->x, e->y, 0.5, 6, COLOR_RGB(178, 170, 152));
    if (e->hp <= 0) {
        e->state = ST_DYING;
        e->as.enemy.timer =
            e->type == ENT_BOSS ? 2.0 : DIE_TIME;
        e->glow = 40;
        adapt_record(e->type, e->combat_time);
        drop_loot(e, el);
        if (e->type == ENT_BOSS)
            audio_play(SND_BOSS_DIE);
        else
            particles_burst(e->x, e->y, 0.4, 14,
                            COLOR_RGB(127, 112, 138));
    }
}

/* The one state machine all three enemies share; their tables of
 * speed/damage/sight differ, and each think function below can add
 * quirks on top. */
static void enemy_brain(Entity *e, World *w)
{
    double d = dist_to_player(e, w);

    if (e->state != ST_DORMANT && e->state != ST_DYING)
        e->combat_time += w->dt; /* fuel for the adaptation heuristic */

    switch (e->state) {
    case ST_DORMANT:
        if (d < e->as.enemy.wake_range
            && map_los(w->map, e->x, e->y, w->player->x, w->player->y))
            wake(e, w);
        break;

    case ST_ALERTED:
        e->as.enemy.timer -= w->dt;
        if (e->as.enemy.timer <= 0.0)
            e->state = ST_PURSUING;
        break;

    case ST_PURSUING: {
        /* recovery pause after a strike: stand still, telegraph over */
        if (e->as.enemy.timer > 0.0) {
            e->as.enemy.timer -= w->dt;
            break;
        }
        if (d < ATTACK_REACH) {
            e->state = ST_ATTACKING;
            e->as.enemy.timer = WINDUP;
            break;
        }

        /* within a couple of tiles and visible: walk straight at the
         * player; grid waypoints would look robotic at close range */
        if (d < 2.2
            && map_los(w->map, e->x, e->y, w->player->x, w->player->y)) {
            double s = e->as.enemy.speed * w->dt / d;
            enemy_move(e, w->map, (w->player->x - e->x) * s,
                       (w->player->y - e->y) * s);
            break;
        }

        /* otherwise follow A* waypoints, re-querying a few times a
         * second — cheap, and adapts as the player runs */
        e->as.enemy.repath -= w->dt;
        double wx = e->as.enemy.wx, wy = e->as.enemy.wy;
        double reached = fabs(e->x - wx) + fabs(e->y - wy);
        if (e->as.enemy.repath <= 0.0 || !e->as.enemy.has_waypoint
            || reached < 0.15) {
            int nx, ny;
            if (pathfind_next(w->map, (int)e->x, (int)e->y,
                              (int)w->player->x, (int)w->player->y,
                              &nx, &ny)) {
                e->as.enemy.wx = nx + 0.5;
                e->as.enemy.wy = ny + 0.5;
                e->as.enemy.has_waypoint = 1;
            }
            e->as.enemy.repath = 0.35;
        }
        if (e->as.enemy.has_waypoint) {
            double mx = e->as.enemy.wx - e->x;
            double my = e->as.enemy.wy - e->y;
            double len = sqrt(mx * mx + my * my);
            if (len > 1e-6) {
                double s = e->as.enemy.speed * w->dt / len;
                if (s > 1.0)
                    s = 1.0;
                enemy_move(e, w->map, mx * s, my * s);
            }
        }
        break;
    }

    case ST_ATTACKING:
        e->as.enemy.timer -= w->dt;
        if (e->as.enemy.timer <= 0.0) {
            /* the strike lands only if the player is still in reach —
             * backpedalling out of a wind-up is a real dodge */
            if (d < ATTACK_REACH + 0.25
                + (e->type == ENT_BOSS ? 0.5 : 0.0)) {
                int dmg = e->as.enemy.strike_damage;
                /* a raised shield turns half the blow */
                if (w->player->blocking)
                    dmg /= 2;
                w->player->hp -= dmg;
                /* naga fangs leave venom: ten seconds of slow drain */
                if (e->type == ENT_NAGA)
                    w->player->poison_t = 10.0;
                audio_play(e->type == ENT_BOSS ? SND_STOMP
                                               : SND_PLAYER_HURT);
            }
            e->state = ST_PURSUING;
            e->as.enemy.timer = RECOVER; /* pause before moving again */
            e->as.enemy.has_waypoint = 0;
            e->as.enemy.repath = 0.0;
        }
        break;

    case ST_DYING:
        e->as.enemy.timer -= w->dt;
        if (e->type == ENT_BOSS) {
            /* Ender-Dragon-style death: golden beams pour out of the
             * crumbling king for the whole two seconds */
            e->glow = 255;
            particles_beam(e->x, e->y, COLOR_RGB(249, 194, 43));
            particles_burst(e->x, e->y, 0.8, 2,
                            COLOR_RGB(255, 240, 150));
        } else {
            e->glow = (uint8_t)(64.0 * (e->as.enemy.timer / DIE_TIME));
        }
        if (e->as.enemy.timer <= 0.0)
            e->remove = 1;
        break;
    }
}

/* ---- per-type think functions ---------------------------------------- */

static void dwarapalaka_think(Entity *e, World *w)
{
    /* STONE FORM: badly wounded, the door-guardian remembers what it
     * is — it freezes back into statue for a moment, unhurtable,
     * knitting a little stone back on. Hit it when it moves. */
    if (e->as.enemy.special > 0.0)
        e->as.enemy.special -= w->dt; /* cooldown between uses */
    if (e->as.enemy.special2 > 0.0) {
        e->as.enemy.special2 -= w->dt;
        e->glow = 20;
        e->kbx = e->kby = 0.0; /* a statue doesn't slide */
        if (e->as.enemy.special2 <= 0.0)
            e->glow = 96; /* the waking glow returns */
        return;           /* stone doesn't think */
    }
    if ((e->state == ST_PURSUING || e->state == ST_ATTACKING)
        && e->hp * 2 <= e->maxhp && e->as.enemy.special <= 0.0) {
        e->as.enemy.special2 = 1.5; /* seconds of stone */
        e->as.enemy.special = 9.0;  /* seconds before the next */
        e->hp += 2;
        if (e->hp > e->maxhp)
            e->hp = e->maxhp;
        play_at(w, e->x, e->y, SND_HIT_STONE);
    }
    enemy_brain(e, w);
}

static void yali_think(Entity *e, World *w)
{
    /* POUNCE: at mid range with a clear line, the temple lion springs
     * — half a second of raw speed. Otherwise it lopes, speed
     * oscillating between crouch and burst like a stalking cat. */
    double d = dist_to_player(e, w);
    if (e->as.enemy.special > 0.0)
        e->as.enemy.special -= w->dt;
    if (e->as.enemy.special2 > 0.0) {
        e->as.enemy.special2 -= w->dt;
        e->as.enemy.speed = 8.0;
    } else {
        if (e->state == ST_PURSUING && e->as.enemy.special <= 0.0
            && d > 2.2 && d < 4.5
            && map_los(w->map, e->x, e->y, w->player->x, w->player->y)) {
            e->as.enemy.special2 = 0.5;
            e->as.enemy.special = 3.5;
            play_at(w, e->x, e->y, SND_ROAR);
        }
        double t = e->x + e->y; /* cheap per-entity phase offset */
        e->as.enemy.speed =
            2.4 + 1.4 * sin(t * 2.0 + e->as.enemy.timer);
    }
    enemy_brain(e, w);
}

static void naga_think(Entity *e, World *w)
{
    /* long sight and quick strikes, but hangs back between them:
     * after striking it keeps its distance briefly (the RECOVER pause
     * set by the shared brain covers this) */
    enemy_brain(e, w);
}

static Entity make_bossfire(double x, double y, double dx, double dy);

static void boss_think(Entity *e, World *w)
{
    /* enrage below half health: faster, angrier, brighter */
    int enraged = e->hp * 2 <= e->maxhp;
    if (e->state != ST_DYING) {
        e->as.enemy.speed = enraged ? 3.0 : 1.8;
        if (enraged && e->state != ST_DORMANT)
            e->glow = 150;
    }

    /* venom-fire volley: three spreading bolts whenever the player
     * keeps distance — closing in is the safer play, like a dragon */
    if (e->state == ST_PURSUING || e->state == ST_ATTACKING) {
        e->as.enemy.special -= w->dt;
        double dx = w->player->x - e->x, dy = w->player->y - e->y;
        double d = sqrt(dx * dx + dy * dy);
        if (e->as.enemy.special <= 0.0 && d > 2.5 && d < 12.0
            && map_los(w->map, e->x, e->y, w->player->x,
                       w->player->y)) {
            dx /= d;
            dy /= d;
            /* centre bolt plus two rotated ~15 degrees */
            const double c = 0.966, s = 0.259;
            entlist_add(w->ents, make_bossfire(e->x, e->y, dx, dy));
            entlist_add(w->ents, make_bossfire(e->x, e->y,
                        dx * c - dy * s, dx * s + dy * c));
            entlist_add(w->ents, make_bossfire(e->x, e->y,
                        dx * c + dy * s, -dx * s + dy * c));
            play_at(w, e->x, e->y, SND_FLAME);
            e->as.enemy.special = enraged ? 2.2 : 4.0;
        }
    }
    enemy_brain(e, w);
}

static void archer_think(Entity *e, World *w)
{
    double d = dist_to_player(e, w);
    int los = map_los(w->map, e->x, e->y, w->player->x, w->player->y);

    /* loose an arrow whenever the player is in the shooting band */
    if ((e->state == ST_PURSUING || e->state == ST_ATTACKING)
        && los && d > 1.5 && d < 10.0) {
        e->as.enemy.special -= w->dt;
        if (e->as.enemy.special <= 0.0) {
            double dx = (w->player->x - e->x) / d;
            double dy = (w->player->y - e->y) / d;
            entlist_add(w->ents,
                        entity_make_arrow(e->x, e->y, dx, dy, 1));
            /* VOLLEY: every fourth draw is three arrows in a fan —
             * the flanking pair rotated ~15 degrees either way */
            if (++e->as.enemy.shots >= 4) {
                e->as.enemy.shots = 0;
                const double c = 0.966, s = 0.259;
                entlist_add(w->ents, entity_make_arrow(e->x, e->y,
                            dx * c - dy * s, dx * s + dy * c, 1));
                entlist_add(w->ents, entity_make_arrow(e->x, e->y,
                            dx * c + dy * s, -dx * s + dy * c, 1));
            }
            play_at(w, e->x, e->y, SND_VEL_SWING);
            e->as.enemy.special = 1.6 / adapt_mult(ENT_ARCHER);
        }
    }

    /* kiting: with the player closing in, back away while firing —
     * an archer's whole battle is keeping the range open */
    if (e->state == ST_PURSUING && d < 2.5 && d > 1e-6 && los) {
        e->combat_time += w->dt;
        double s = e->as.enemy.speed * w->dt / d;
        enemy_move(e, w->map, (e->x - w->player->x) * s,
                   (e->y - w->player->y) * s);
        return; /* skip the brain: no melee charge */
    }
    enemy_brain(e, w);
}

static void knight_think(Entity *e, World *w)
{
    /* nothing fancy: mass, armour and a heavy blade ARE the tactic */
    enemy_brain(e, w);
}

static void arrow_think(Entity *e, World *w)
{
    e->as.flame.life -= w->dt;
    if (e->as.flame.life <= 0.0) {
        e->remove = 1;
        return;
    }
    double nx = e->x + e->as.flame.vx * w->dt;
    double ny = e->y + e->as.flame.vy * w->dt;
    if (map_solid(w->map, (int)nx, (int)ny)) {
        particles_burst(e->x, e->y, 0.5, 3, COLOR_RGB(178, 170, 152));
        e->remove = 1;
        return;
    }
    e->x = nx;
    e->y = ny;

    if (e->as.flame.hostile) {
        double dx = w->player->x - e->x, dy = w->player->y - e->y;
        if (dx * dx + dy * dy < 0.45 * 0.45) {
            w->player->hp -= w->player->blocking ? 4 : 8;
            audio_play(SND_PLAYER_HURT);
            e->remove = 1;
        }
    } else {
        for (int i = 0; i < w->ents->count; i++) {
            Entity *o = &w->ents->items[i];
            if (!ENTITY_IS_ENEMY(o) || o->state == ST_DYING)
                continue;
            double dx = o->x - e->x, dy = o->y - e->y;
            if (dx * dx + dy * dy < 0.45 * 0.45) {
                double len = sqrt(e->as.flame.vx * e->as.flame.vx
                                  + e->as.flame.vy * e->as.flame.vy);
                entity_damage(o, w->ents, 4, e->as.flame.vx / len,
                              e->as.flame.vy / len);
                e->remove = 1;
                return;
            }
        }
    }
}

static void item_think(Entity *e, World *w)
{
    /* dropped loot glimmers so it catches the eye across a room */
    e->combat_time += w->dt; /* reused here as a simple clock */
    e->glow = (uint8_t)(150.0 + 60.0 * sin(e->combat_time * 4.0));
}

static void bossfire_think(Entity *e, World *w)
{
    e->as.flame.life -= w->dt;
    if (e->as.flame.life <= 0.0) {
        e->remove = 1;
        return;
    }
    double nx = e->x + e->as.flame.vx * w->dt;
    double ny = e->y + e->as.flame.vy * w->dt;
    if (map_solid(w->map, (int)nx, (int)ny)) {
        particles_burst(e->x, e->y, 0.4, 5, COLOR_RGB(145, 219, 105));
        e->remove = 1;
        return;
    }
    e->x = nx;
    e->y = ny;

    double dx = w->player->x - e->x, dy = w->player->y - e->y;
    if (dx * dx + dy * dy < 0.55 * 0.55) {
        w->player->hp -= w->player->blocking ? 5 : 10;
        audio_play(SND_PLAYER_HURT);
        e->remove = 1;
    }
}

static void lamp_think(Entity *e, World *w)
{
    /* Two incommensurate sine frequencies sum to a flicker that never
     * visibly repeats. Mapped into glow 190..250: bright, unsteady. */
    e->as.lamp.phase += w->dt;
    double t = e->as.lamp.phase;
    double f = 0.5 + 0.3 * sin(t * 9.1) + 0.2 * sin(t * 23.7);
    e->glow = (uint8_t)(190.0 + f * 60.0);
}

static void flame_think(Entity *e, World *w)
{
    e->as.flame.life -= w->dt;
    if (e->as.flame.life <= 0.0) {
        e->remove = 1;
        return;
    }

    double nx = e->x + e->as.flame.vx * w->dt;
    double ny = e->y + e->as.flame.vy * w->dt;
    if (map_solid(w->map, (int)nx, (int)ny)) {
        e->remove = 1; /* burst on stone */
        return;
    }
    e->x = nx;
    e->y = ny;

    /* burn the first enemy we pass through */
    for (int i = 0; i < w->ents->count; i++) {
        Entity *o = &w->ents->items[i];
        if (!ENTITY_IS_ENEMY(o) || o->state == ST_DYING)
            continue;
        double dx = o->x - e->x, dy = o->y - e->y;
        if (dx * dx + dy * dy < 0.45 * 0.45) {
            double len = sqrt(e->as.flame.vx * e->as.flame.vx
                              + e->as.flame.vy * e->as.flame.vy);
            entity_damage(o, w->ents, 3, e->as.flame.vx / len,
                          e->as.flame.vy / len);
            e->remove = 1;
            return;
        }
    }
}

/* ---- constructors ---------------------------------------------------- */

static Entity make_enemy(EntityType type, double x, double y, int hp,
                         double wake_range, double speed, int damage,
                         int tex)
{
    Entity e = {0};
    e.type = type;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = tex;

    /* the adaptation multiplier bites here: every spawn of a type
     * the player has been beating comes back a little harder */
    double m = adapt_mult(type);
    e.hp = (int)(hp * m + 0.5);
    if (e.hp < 1)
        e.hp = 1;
    e.maxhp = e.hp;
    e.as.enemy.wake_range = wake_range;
    e.as.enemy.speed = speed * (0.7 + 0.3 * m);
    e.as.enemy.strike_damage = damage;
    return e;
}

Entity entity_make_dwarapalaka(double x, double y)
{
    Entity e = make_enemy(ENT_DWARAPALAKA, x, y, 10, 7.0, 1.4, 18,
                          TEX_GUARDIAN);
    e.think = dwarapalaka_think;
    return e;
}

Entity entity_make_yali(double x, double y)
{
    Entity e = make_enemy(ENT_YALI, x, y, 4, 6.0, 3.0, 8, TEX_YALI);
    e.think = yali_think;
    return e;
}

Entity entity_make_naga(double x, double y)
{
    Entity e = make_enemy(ENT_NAGA, x, y, 6, 9.5, 2.1, 12, TEX_NAGA);
    e.think = naga_think;
    /* low and long: a crawler hugging the flagstones, not an upright
     * statue gliding about — the renderer adds the S-curve slither */
    e.height = 0.72;
    e.widthm = 1.2;
    return e;
}

Entity entity_make_lamp(double x, double y)
{
    Entity e = {0};
    e.type = ENT_LAMP;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_LAMP;
    e.glow = 220;
    e.think = lamp_think;
    e.as.lamp.phase = x * 3.7 + y * 1.3; /* desync neighbouring lamps */
    return e;
}

static double plamp_clock = 0.0; /* shared flicker clock */

static void plamp_think(Entity *e, World *w)
{
    /* lit puzzle lamps flicker like ordinary lamps; unlit ones are
     * cold stone with only their gold pips catching light */
    if (e->as.plamp.lit) {
        plamp_clock += w->dt * 0.25; /* several lamps share it; tiny
                                      * increments keep it gentle */
        double t = plamp_clock + e->x * 3.7 + e->y * 1.3;
        e->glow = (uint8_t)(205.0 + 30.0 * sin(t * 9.1));
    } else {
        e->glow = 60;
    }
}

Entity entity_make_puzzle_lamp(double x, double y, int order)
{
    Entity e = {0};
    e.type = ENT_PUZZLE_LAMP;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_PLAMP_1 + (order - 1);
    e.glow = 60;
    e.think = plamp_think;
    e.as.plamp.order = order;
    e.as.plamp.lit = 0;
    return e;
}

Entity entity_make_lever(double x, double y)
{
    Entity e = {0};
    e.type = ENT_LEVER;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_LEVER_OFF;
    e.glow = 40;
    e.think = NULL;
    e.as.lever.on = 0;
    return e;
}

Entity entity_make_shrine(double x, double y)
{
    Entity e = {0};
    e.type = ENT_SHRINE;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_SHRINE_OFF;
    e.glow = 50;
    e.think = NULL;
    e.as.shrine.lit = 0;
    return e;
}

Entity entity_make_flame(double x, double y, double dirx, double diry)
{
    Entity e = {0};
    e.type = ENT_FLAME;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_FLAMEBALL;
    e.glow = 255;
    e.think = flame_think;
    e.as.flame.vx = dirx * 9.0;
    e.as.flame.vy = diry * 9.0;
    e.as.flame.life = 1.6;
    return e;
}

static Entity make_bossfire(double x, double y, double dx, double dy)
{
    Entity e = {0};
    e.type = ENT_BOSSFIRE;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_VENOM;
    e.glow = 255;
    e.widthm = 0.7;
    e.height = 0.7;
    e.think = bossfire_think;
    e.as.flame.vx = dx * 6.5;
    e.as.flame.vy = dy * 6.5;
    e.as.flame.life = 3.0;
    return e;
}

Entity entity_make_boss(double x, double y)
{
    Entity e = make_enemy(ENT_BOSS, x, y, 60, 10.0, 1.8, 24,
                          TEX_BOSS);
    e.think = boss_think;
    e.height = 2.0; /* twice the height of a wall — he fills the hall */
    e.widthm = 1.7;
    e.as.enemy.special = 2.0;
    return e;
}

Entity entity_make_archer(double x, double y)
{
    Entity e = make_enemy(ENT_ARCHER, x, y, 5, 8.5, 2.2, 6,
                          TEX_ARCHER);
    e.think = archer_think;
    e.as.enemy.special = 1.0; /* first arrow comes quickly */
    return e;
}

Entity entity_make_knight(double x, double y)
{
    Entity e = make_enemy(ENT_KNIGHT, x, y, 14, 6.5, 1.6, 15,
                          TEX_KNIGHT);
    e.think = knight_think;
    e.widthm = 1.15;
    return e;
}

Entity entity_make_arrow(double x, double y, double dirx, double diry,
                         int hostile)
{
    Entity e = {0};
    e.type = ENT_ARROW;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_ARROW;
    e.widthm = 0.5;
    e.height = 0.45;
    e.think = arrow_think;
    double v = hostile ? 10.0 : 13.0;
    e.as.flame.vx = dirx * v;
    e.as.flame.vy = diry * v;
    e.as.flame.life = 1.5;
    e.as.flame.hostile = hostile;
    return e;
}

/* which sprite a ground item wears */
static int item_tex(int item_type)
{
    switch (item_type) {
    case ITEM_SWORD:    return TEX_SWORD;
    case ITEM_BOW:      return TEX_BOW;
    case ITEM_SHIELD:   return TEX_SHIELD;
    case ITEM_POT_HEAL: return TEX_POT_HEAL;
    case ITEM_POT_STR:  return TEX_POT_STR;
    case ITEM_POT_SPD:  return TEX_POT_SPD;
    case ITEM_FLAME:    return TEX_FLAMEBALL;
    default:            return TEX_VEL;
    }
}

Entity entity_make_item(double x, double y, int item_type)
{
    Entity e = {0};
    e.type = ENT_ITEM;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = item_tex(item_type);
    e.widthm = 0.5;
    e.height = 0.55;
    e.glow = 170;
    e.think = item_think;
    e.as.item.what = item_type;
    return e;
}

Entity entity_make_chest(double x, double y)
{
    Entity e = {0};
    e.type = ENT_CHEST;
    e.state = ST_DORMANT;
    e.x = x;
    e.y = y;
    e.tex = TEX_CHEST;
    e.widthm = 0.85;
    e.height = 0.8;
    e.glow = 60;
    e.think = NULL;
    e.as.chest.opened = 0;
    return e;
}
