#include <SDL.h>
#include <stdio.h>

#include "audio.h"
#include "config.h"
#include "entity.h"
#include "framebuffer.h"
#include "inventory.h"
#include "map.h"
#include "particles.h"
#include "pathfind.h"
#include "player.h"
#include "puzzle.h"
#include "save.h"
#include "text.h"
#include "raycaster.h"
#include "texture.h"
#include "util.h"

/* M_PI is not part of strict C99 (it's a POSIX extension), so we
 * define our own rather than rely on the compiler being lenient. */
#define PI 3.14159265358979323846

#define NUM_FLOORS 3

/* each floor of the descent is larger than the last */
static const int floor_dims[NUM_FLOORS][2] = {
    { 40, 30 }, { 50, 36 }, { 58, 42 }
};

/* Turn the generator's spawn list into live entities. Reused on every
 * regeneration, so it resets the list first. */
static void spawn_entities(EntityList *el, const Map *m)
{
    el->count = 0;
    for (int i = 0; i < m->nspawns; i++) {
        const Spawn *s = &m->spawns[i];
        switch (s->kind) {
        case SPAWN_DWARAPALAKA:
            entlist_add(el, entity_make_dwarapalaka(s->x, s->y));
            break;
        case SPAWN_YALI:
            entlist_add(el, entity_make_yali(s->x, s->y));
            break;
        case SPAWN_NAGA:
            entlist_add(el, entity_make_naga(s->x, s->y));
            break;
        case SPAWN_LAMP:
            entlist_add(el, entity_make_lamp(s->x, s->y));
            break;
        case SPAWN_PUZZLE_LAMP:
            entlist_add(el, entity_make_puzzle_lamp(s->x, s->y,
                                                    s->arg));
            break;
        case SPAWN_LEVER:
            entlist_add(el, entity_make_lever(s->x, s->y));
            break;
        case SPAWN_SHRINE:
            entlist_add(el, entity_make_shrine(s->x, s->y));
            break;
        case SPAWN_BOSS:
            entlist_add(el, entity_make_boss(s->x, s->y));
            break;
        case SPAWN_ARCHER:
            entlist_add(el, entity_make_archer(s->x, s->y));
            break;
        case SPAWN_KNIGHT:
            entlist_add(el, entity_make_knight(s->x, s->y));
            break;
        case SPAWN_CHEST:
            entlist_add(el, entity_make_chest(s->x, s->y));
            break;
        }
    }
}

/* which sprite the held item shows, in hand and in the hotbar */
static TextureId item_texture(ItemType t)
{
    switch (t) {
    case ITEM_VEL:      return TEX_VEL;
    case ITEM_FLAME:    return TEX_FLAMEBALL;
    case ITEM_SWORD:    return TEX_SWORD;
    case ITEM_BOW:      return TEX_BOW;
    case ITEM_SHIELD:   return TEX_SHIELD;
    case ITEM_POT_HEAL: return TEX_POT_HEAL;
    case ITEM_POT_STR:  return TEX_POT_STR;
    case ITEM_POT_SPD:  return TEX_POT_SPD;
    default:            return TEX_VEL;
    }
}

/* Build (or rebuild) one floor of the temple: same seed reproduces
 * the same floor, which also powers the death-reset. */
static int load_floor(Map *map, EntityList *ents, Player *player,
                      Puzzle *pz, uint32_t seed, int floor_num)
{
    int fw = floor_dims[floor_num - 1][0];
    int fh = floor_dims[floor_num - 1][1];
    map_free(map);
    /* one architectural style per floor; the King waits on the last */
    if (map_generate(map, fw, fh, seed,
                     (MapStyle)(floor_num - 1),
                     floor_num == NUM_FLOORS) != 0)
        return -1;
    if (pathfind_init(fw, fh) != 0)
        return -1;
    player_init(player, map->spawn_x, map->spawn_y, map->spawn_angle);
    spawn_entities(ents, map);
    puzzle_init(pz, map);
    particles_reset();
    return 0;
}

static const char *floor_names[NUM_FLOORS] = {
    "THE PILLARED HALLS", "THE MAZE", "THE KINGS CITADEL"
};

static void draw_centered(Framebuffer *fb, int y, int scale,
                          uint32_t color, const char *s)
{
    text_draw_shadow(fb, (fb->w - text_width(s, scale)) / 2, y,
                     scale, color, s);
}

/* ---- Minecraft-style HUD -------------------------------------------- */

/* one heart, 7x6 pixels; fill: 0 empty, 1 half, 2 full. The full
 * colour is a parameter because poisoned hearts turn sickly green,
 * exactly as Minecraft signals venom. */
static void draw_heart(Framebuffer *fb, int x, int y, int fill,
                       uint32_t full_col)
{
    static const char *rows[6] = {
        ".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X...",
    };
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 7; c++) {
            if (rows[r][c] != 'X')
                continue;
            uint32_t col;
            if (fill == 2 || (fill == 1 && c < 4))
                col = full_col;
            else
                col = COLOR_RGB(62, 53, 70);
            fb_put(fb, x + c, y + r, col);
        }
}

/* 16x16 hotbar icon: every 4th pixel of a 64px sprite */
static void draw_icon(Framebuffer *fb, const uint32_t *tex,
                      int dx, int dy)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            uint32_t c = tex[(y * 4) * TEX_SIZE + x * 4];
            if ((c >> 24) != 0)
                fb_put(fb, dx + x, dy + y, c);
        }
}

/* one 21x21 item slot with icon, stack count and selection ring */
static void draw_slot(Framebuffer *fb, int x, int y,
                      const ItemStack *s, int selected)
{
    fb_fill_rect(fb, x, y, 21, 21, COLOR_RGB(62, 53, 70));
    fb_fill_rect(fb, x + 1, y + 1, 19, 19, COLOR_RGB(98, 85, 101));
    if (selected) {
        fb_fill_rect(fb, x, y, 21, 2, COLOR_RGB(255, 255, 255));
        fb_fill_rect(fb, x, y + 19, 21, 2, COLOR_RGB(255, 255, 255));
        fb_fill_rect(fb, x, y, 2, 21, COLOR_RGB(255, 255, 255));
        fb_fill_rect(fb, x + 19, y, 2, 21, COLOR_RGB(255, 255, 255));
    }
    if (s->count > 0) {
        draw_icon(fb, texture_pixels(item_texture(s->type)),
                  x + 3, y + 3);
        if (s->count > 1) {
            char n[4];
            snprintf(n, sizeof n, "%d", s->count);
            text_draw_shadow(fb, x + 14, y + 13, 1,
                             COLOR_RGB(255, 255, 255), n);
        }
    }
}

#define HOTBAR_X 98 /* 5 slots * 24px + margins, centred */

static void draw_hud(Framebuffer *fb, const Player *p)
{
    /* hearts: 10 of them, 10 hp each, half at 5; venom greens them */
    uint32_t hc = p->poison_t > 0.0 ? COLOR_RGB(117, 167, 67)
                                    : COLOR_RGB(232, 59, 59);
    for (int i = 0; i < 10; i++) {
        int fill = p->hp >= (i + 1) * 10 ? 2
                 : p->hp >= i * 10 + 5 ? 1 : 0;
        draw_heart(fb, HOTBAR_X + 14 + i * 9, 168, fill, hc);
    }

    for (int s = 0; s < HOTBAR_SLOTS; s++)
        draw_slot(fb, HOTBAR_X + s * 24, 177, &p->inv.hotbar[s],
                  s == p->inv.selected);
    /* the left hand, drawn beside the hotbar; ringed while raised */
    draw_slot(fb, HOTBAR_X - 26, 177, &p->inv.offhand, p->blocking);

    /* active effects, Minecraft-style, in the top corner */
    int ex = 4, ey = 4;
    if (p->poison_t > 0.0) {
        draw_icon(fb, texture_pixels(TEX_VENOM), ex, ey);
        fb_fill_rect(fb, ex, ey + 17,
                     (int)(16.0 * p->poison_t / 10.0), 2,
                     COLOR_RGB(117, 167, 67));
        ex += 20;
    }
    if (p->buff_str_t > 0.0) {
        draw_icon(fb, texture_pixels(TEX_POT_STR), ex, ey);
        fb_fill_rect(fb, ex, ey + 17, (int)(16.0 * p->buff_str_t / 20.0),
                     2, COLOR_RGB(251, 107, 29));
        ex += 20;
    }
    if (p->buff_spd_t > 0.0) {
        draw_icon(fb, texture_pixels(TEX_POT_SPD), ex, ey);
        fb_fill_rect(fb, ex, ey + 17, (int)(16.0 * p->buff_spd_t / 20.0),
                     2, COLOR_RGB(48, 225, 185));
    }

    /* crosshair */
    fb_fill_rect(fb, 158, 99, 5, 1, COLOR_RGB(230, 230, 230));
    fb_fill_rect(fb, 160, 97, 1, 5, COLOR_RGB(230, 230, 230));
}

/* ---- the I-key inventory screen: Minecraft drag-and-drop ------------
 * One "cursor stack" follows the mouse, and every gesture a Minecraft
 * player's hands already know is supported:
 *
 *   left click      pick the whole stack up / put it down / swap
 *   press and drag  drop on another slot: move there, or trade places
 *                   with what it holds; drop off every slot to put it back
 *   shift + left    send the stack straight across, storage <-> hotbar
 *   right click     take half; or, holding a stack, lay down one
 *   1-5 while over  swap that slot with that hotbar slot
 *      a slot
 *
 * The storage grid is deliberately not usable in a fight — getting a
 * potion out of it and into the hotbar is what shift-click is for.
 * The offhand slot on the right is the left hand: a shield there
 * blocks while sneaking, leaving the right hand free to fight. */

/* every slot's screen rectangle, computed one way for draw AND click
 * so the two can never disagree. Returns the stack, or NULL. */
static ItemStack *slot_at(Inventory *inv, int idx, int *sx, int *sy)
{
    if (idx < STORE_SLOTS) { /* the 2x5 storage grid */
        *sx = 88 + (idx % 5) * 24;
        *sy = 62 + (idx / 5) * 24;
        return &inv->store[idx];
    }
    idx -= STORE_SLOTS;
    if (idx < HOTBAR_SLOTS) {
        *sx = 88 + idx * 24;
        *sy = 118;
        return &inv->hotbar[idx];
    }
    if (idx == HOTBAR_SLOTS) { /* offhand, right of the hotbar */
        *sx = 216;
        *sy = 118;
        return &inv->offhand;
    }
    return NULL;
}
#define SLOT_TOTAL (STORE_SLOTS + HOTBAR_SLOTS + 1)

/* which slot is under this framebuffer coordinate, or -1 for none */
static int slot_index_at(Inventory *inv, int mx, int my)
{
    for (int i = 0; i < SLOT_TOTAL; i++) {
        int x, y;
        slot_at(inv, i, &x, &y);
        if (mx >= x && mx < x + 21 && my >= y && my < y + 21)
            return i;
    }
    return -1;
}

/* pour as much of src into dst as the stack cap allows */
static void stack_merge(ItemStack *dst, ItemStack *src)
{
    int room = STACK_MAX - dst->count;
    int moved = src->count < room ? src->count : room;
    dst->count += moved;
    src->count -= moved;
    if (src->count <= 0)
        *src = (ItemStack){ ITEM_NONE, 0 };
}

/* Shift-click: send a stack straight across between storage and the
 * hotbar without it ever touching the cursor. This is how Minecraft
 * gets something into your hands in one gesture, and it is the whole
 * point of having a storage grid you can't attack out of. Returns 1
 * if anything actually moved. */
static int quick_move(Inventory *inv, int idx)
{
    int sx, sy;
    ItemStack *src = slot_at(inv, idx, &sx, &sy);
    if (src->count <= 0)
        return 0;

    /* storage sends to the hotbar; hotbar and offhand send back */
    int lo = (idx < STORE_SLOTS) ? STORE_SLOTS : 0;
    int hi = (idx < STORE_SLOTS) ? STORE_SLOTS + HOTBAR_SLOTS
                                 : STORE_SLOTS;
    int moved = 0;

    /* top up a matching stack first, so potions gather rather than
     * scattering one per slot */
    if (item_stacks(src->type)) {
        for (int i = lo; i < hi && src->count > 0; i++) {
            int x, y;
            ItemStack *dst = slot_at(inv, i, &x, &y);
            if (dst->count <= 0 || dst->type != src->type
                || dst->count >= STACK_MAX)
                continue;
            stack_merge(dst, src);
            moved = 1;
        }
    }
    /* whatever is left goes to the first empty slot */
    for (int i = lo; i < hi && src->count > 0; i++) {
        int x, y;
        ItemStack *dst = slot_at(inv, i, &x, &y);
        if (dst->count > 0)
            continue;
        *dst = *src;
        *src = (ItemStack){ ITEM_NONE, 0 };
        moved = 1;
        break;
    }
    return moved;
}

static void draw_inventory(Framebuffer *fb, Inventory *inv,
                           const ItemStack *cursor, int mx, int my)
{
    fb_fill_rect(fb, 78, 44, 164, 116, COLOR_RGB(62, 53, 70));
    fb_fill_rect(fb, 80, 46, 160, 112, COLOR_RGB(127, 112, 138));
    draw_centered(fb, 50, 1, COLOR_RGB(255, 255, 255), "INVENTORY");

    const char *hover = NULL;
    int hover_x = 0, hover_y = 0, hovering = 0;
    for (int i = 0; i < SLOT_TOTAL; i++) {
        int x, y;
        ItemStack *s = slot_at(inv, i, &x, &y);
        draw_slot(fb, x, y, s,
                  i == STORE_SLOTS + inv->selected);
        if (mx >= x && mx < x + 21 && my >= y && my < y + 21) {
            hover_x = x;
            hover_y = y;
            hovering = 1;
            if (s->count > 0)
                hover = item_name(s->type);
        }
    }
    /* outline the slot under the pointer, so it is never ambiguous
     * which one a click is about to land on */
    if (hovering) {
        const uint32_t hl = COLOR_RGB(255, 240, 150);
        fb_fill_rect(fb, hover_x, hover_y, 21, 1, hl);
        fb_fill_rect(fb, hover_x, hover_y + 20, 21, 1, hl);
        fb_fill_rect(fb, hover_x, hover_y, 1, 21, hl);
        fb_fill_rect(fb, hover_x + 20, hover_y, 1, 21, hl);
    }
    text_draw_shadow(fb, 213, 141, 1, COLOR_RGB(222, 210, 190), "OFF");

    draw_centered(fb, 150, 1, COLOR_RGB(255, 240, 150),
                  hover ? hover : "SHIFT-CLICK TO HOTBAR");

    /* the picked-up stack rides on the mouse */
    if (cursor->count > 0) {
        draw_icon(fb, texture_pixels(item_texture(cursor->type)),
                  mx - 8, my - 8);
        if (cursor->count > 1) {
            char n[4];
            snprintf(n, sizeof n, "%d", cursor->count);
            text_draw_shadow(fb, mx + 4, my + 2, 1,
                             COLOR_RGB(255, 255, 255), n);
        }
    }

    /* Draw our own pointer last, on top of everything. The OS cursor
     * is restored when relative mouse mode ends, but it is not always
     * where the player expects after a mouse-look session — drawing
     * the game's own idea of the position removes all doubt. */
    fb_fill_rect(fb, mx, my, 1, 6, COLOR_RGB(20, 16, 14));
    fb_fill_rect(fb, mx, my, 6, 1, COLOR_RGB(20, 16, 14));
    fb_fill_rect(fb, mx, my, 1, 5, COLOR_RGB(255, 255, 255));
    fb_fill_rect(fb, mx, my, 5, 1, COLOR_RGB(255, 255, 255));
}

/* One click on the inventory screen, in framebuffer coordinates.
 *   button : SDL_BUTTON_LEFT takes the whole stack, RIGHT splits it
 *   shift  : quick-move straight across, never touching the cursor
 * Returns the slot acted on, or -1 if the click missed every slot. */
static int inventory_click(Inventory *inv, ItemStack *cursor,
                           int mx, int my, int button, int shift)
{
    int idx = slot_index_at(inv, mx, my);
    if (idx < 0)
        return -1;

    int sx, sy;
    ItemStack *s = slot_at(inv, idx, &sx, &sy);

    /* shift-click only makes sense with an empty hand */
    if (shift && button == SDL_BUTTON_LEFT && cursor->count == 0) {
        if (quick_move(inv, idx))
            audio_play(SND_PICKUP);
        return idx;
    }

    if (button == SDL_BUTTON_RIGHT) {
        if (cursor->count == 0) {
            /* take half, rounded up — the classic right-click split */
            if (s->count > 0) {
                int half = (s->count + 1) / 2;
                cursor->type = s->type;
                cursor->count = half;
                s->count -= half;
                if (s->count <= 0)
                    *s = (ItemStack){ ITEM_NONE, 0 };
                audio_play(SND_PICKUP);
            }
        } else if (s->count == 0) {
            /* lay down a single item from the held stack */
            s->type = cursor->type;
            s->count = 1;
            if (--cursor->count <= 0)
                *cursor = (ItemStack){ ITEM_NONE, 0 };
            audio_play(SND_PICKUP);
        } else if (s->type == cursor->type && item_stacks(s->type)
                   && s->count < STACK_MAX) {
            s->count++;
            if (--cursor->count <= 0)
                *cursor = (ItemStack){ ITEM_NONE, 0 };
            audio_play(SND_PICKUP);
        }
        return idx;
    }

    if (cursor->count > 0 && s->count > 0 && item_stacks(cursor->type)
        && s->type == cursor->type && s->count < STACK_MAX) {
        /* same potion: pour into the slot, keep any overflow held */
        stack_merge(s, cursor);
    } else {
        /* pick up / put down / swap: one three-way exchange
         * covers all three cases */
        ItemStack t = *s;
        *s = *cursor;
        *cursor = t;
    }
    audio_play(SND_PICKUP);
    return idx;
}

/* Finish a drag that began by lifting a whole stack off slot `from`
 * into an empty hand, so `from` should be empty right now. Where the
 * stack lands:
 *   an empty slot      it moves there
 *   the same potion    it pours in; any overflow goes back home
 *   a different item   the two trade places: that item goes home
 *   off every slot     it goes back home
 * The hand always ends empty — dragging onto a full slot used to
 * leave the other item stuck to the pointer instead of swapping.
 * Returns 0 without touching anything if `from` was refilled during
 * the drag (a 1-5 swap while the button was held), so the caller can
 * fall back to a plain click rather than overwrite that item. */
static int inventory_drop(Inventory *inv, ItemStack *cursor,
                          int from, int to)
{
    int x, y;
    ItemStack *home = slot_at(inv, from, &x, &y);
    if (home->count > 0)
        return 0;

    if (to < 0) {
        *home = *cursor;
    } else {
        ItemStack *dst = slot_at(inv, to, &x, &y);
        if (dst->count > 0 && dst->type == cursor->type
            && item_stacks(cursor->type)) {
            stack_merge(dst, cursor);
            if (cursor->count > 0)
                *home = *cursor;
        } else {
            *home = *dst; /* an empty target leaves home empty too */
            *dst = *cursor;
        }
    }
    *cursor = (ItemStack){ ITEM_NONE, 0 };
    audio_play(SND_PICKUP);
    return 1;
}

/* Melee: hit the nearest enemy within reach and roughly in front of
 * the player (dot product cone test). Damage/reach come from the
 * held weapon; the strength potion doubles the blow. */
static void swing_melee(EntityList *el, const Player *p, int damage,
                        double reach)
{
    audio_play(SND_VEL_SWING);
    /* the swing itself is visible from any camera: a puff of bright
     * motes along the blade's path (the third-person swing "arc") */
    particles_burst(p->x + p->dirX * 1.0, p->y + p->dirY * 1.0,
                    0.35, 5, COLOR_RGB(255, 240, 150));
    if (p->buff_str_t > 0.0)
        damage *= 2;
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->items[i];
        if (!ENTITY_IS_ENEMY(e))
            continue;
        double dx = e->x - p->x, dy = e->y - p->y;
        double d = SDL_sqrt(dx * dx + dy * dy);
        if (d > reach || d < 1e-6)
            continue;
        /* cos of angle between facing and target > 0.7 (~45° cone) */
        if ((dx * p->dirX + dy * p->dirY) / d > 0.7) {
            /* knockback away along the strike direction */
            entity_damage(e, el, damage, p->dirX, p->dirY);
            return; /* one strike, one target */
        }
    }
}

#define MOVE_SPEED  3.2  /* tiles per second */
#define MOUSE_SENS  0.0022 /* radians per pixel of mouse travel */
#define KEY_TURN    2.4  /* radians per second on arrow keys */

/* A minimap that looks like one: carved stone frame, fog of war
 * (only explored tiles appear), icons for what matters. Drawn 2
 * pixels per tile so even the big floors fit the corner. */
static void draw_minimap(Framebuffer *fb, const Map *m, const Player *p,
                         const EntityList *el)
{
    const int cell = 2, ox = 6, oy = 6;
    int mw = m->w * cell, mh = m->h * cell;

    /* stone frame around a parchment-dark ground */
    fb_fill_rect(fb, ox - 3, oy - 3, mw + 6, mh + 6,
                 COLOR_RGB(150, 104, 48));
    fb_fill_rect(fb, ox - 1, oy - 1, mw + 2, mh + 2,
                 COLOR_RGB(24, 18, 14));

    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            if (!map_seen(m, x, y))
                continue; /* fog of war: the unexplored stays dark */
            uint32_t c;
            switch (map_tile(m, x, y)) {
            case TILE_DOOR:    c = COLOR_RGB(249, 194, 43); break;
            case TILE_SANCTUM: c = COLOR_RGB(186, 96, 50); break;
            case TILE_ROTOR:   c = COLOR_RGB(255, 240, 150); break;
            case TILE_FLOOR:
                c = map_floor(m, x, y) == FLOOR_WATER
                    ? COLOR_RGB(14, 175, 155)
                    : COLOR_RGB(84, 76, 70);
                break;
            default:           c = COLOR_RGB(171, 148, 122); break;
            }
            fb_fill_rect(fb, ox + x * cell, oy + y * cell,
                         cell, cell, c);
        }
    }

    /* icons, only in explored territory */
    for (int i = 0; i < el->count; i++) {
        const Entity *e = &el->items[i];
        if (!map_seen(m, (int)e->x, (int)e->y))
            continue;
        int ix = ox + (int)(e->x * cell) - 1;
        int iy = oy + (int)(e->y * cell) - 1;
        if (e->type == ENT_CHEST && !e->as.chest.opened)
            fb_fill_rect(fb, ix, iy, 3, 3, COLOR_RGB(249, 194, 43));
        else if (e->type == ENT_SHRINE)
            fb_fill_rect(fb, ix, iy, 3, 3, COLOR_RGB(255, 255, 255));
        else if (e->type == ENT_PUZZLE_LAMP && !e->as.plamp.lit)
            fb_fill_rect(fb, ix, iy, 2, 2, COLOR_RGB(251, 107, 29));
        else if (ENTITY_IS_ENEMY(e) && e->state != ST_DORMANT
                 && e->state != ST_DYING)
            fb_fill_rect(fb, ix, iy, 2, 2, COLOR_RGB(232, 59, 59));
    }

    /* the keeper: dot plus facing line */
    int px = ox + (int)(p->x * cell);
    int py = oy + (int)(p->y * cell);
    for (int i = 0; i < 5; i++)
        fb_put(fb, px + (int)(p->dirX * i), py + (int)(p->dirY * i),
               COLOR_RGB(255, 240, 150));
    fb_fill_rect(fb, px - 1, py - 1, 3, 3, COLOR_RGB(48, 225, 185));

    /* compass: north is up */
    text_draw_shadow(fb, ox + mw - 6, oy + 1, 1,
                     COLOR_RGB(255, 255, 255), "N");
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Config cfg;
    config_load(&cfg, "kovil.ini");

    SDL_Window *window = SDL_CreateWindow(
        "Kovil",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        FB_WIDTH * cfg.window_scale, FB_HEIGHT * cfg.window_scale,
        SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* "0" = nearest-neighbour when the texture is scaled up: hard-edged
     * chunky pixels instead of a blurry bilinear smear. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    /* STREAMING because we rewrite every pixel from the CPU each frame;
     * ARGB8888 matches our COLOR_RGB packing byte for byte. */
    SDL_Texture *screen = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, FB_WIDTH, FB_HEIGHT);
    if (!screen) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Framebuffer *fb = fb_create();
    if (!fb) {
        fprintf(stderr, "out of memory allocating framebuffer\n");
        SDL_DestroyTexture(screen);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (textures_init() != 0) {
        fprintf(stderr, "out of memory generating textures\n");
        fb_destroy(fb);
        SDL_DestroyTexture(screen);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    palette_init();
    audio_init(); /* failure is fine: the game just runs silently */
    audio_set_volume((float)cfg.volume);

    uint32_t seed = SDL_GetTicks() ^ 0xC01FEEDu;
    Map map = {0};
    Player player;
    Puzzle pz;
    int floor_num = 1;

    EntityList ents;
    if (entlist_init(&ents) != 0) {
        fprintf(stderr, "out of memory allocating entities\n");
        audio_shutdown();
        textures_shutdown();
        fb_destroy(fb);
        SDL_DestroyTexture(screen);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    adapt_reset();
    if (load_floor(&map, &ents, &player, &pz, seed, floor_num) != 0) {
        fprintf(stderr, "map generation failed\n");
        entlist_free(&ents);
        map_free(&map);
        audio_shutdown();
        textures_shutdown();
        fb_destroy(fb);
        SDL_DestroyTexture(screen);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* light flicker source: seeded from the clock so each run of the
     * flame is different (unlike textures, this SHOULD vary) */
    Rng flick_rng;
    rng_seed(&flick_rng, SDL_GetTicks() + 1);
    double flick = 1.0;
    double dread = 0.0;
    double hurt = 0.0;       /* red damage-flash amount */
    double death_t = 0.0;    /* death fade timer */
    double melee_cd = 0.0, flame_cd = 0.0;
    double attack_anim = 0.0;
    double bob_phase = 0.0;
    int prev_hp = PLAYER_MAX_HP;
    double descend_t = 0.0;  /* gold fade when a floor is completed */
    int descended = 0;       /* floor already swapped this fade? */
    int won = 0;
    double intro_t = 9.0;    /* objective text shown at floor start */
    double shake = 0.0;      /* screen shake amount, decays */
    int inv_open = 0;        /* the I-key inventory screen */
    int show_controls = 0;   /* the C-key controls panel */
    int third_person = 0;    /* the V-key camera */
    double drink_t = 0.0;    /* potion-drinking animation timer */
    TextureId drink_icon = TEX_POT_HEAL;
    uint32_t drink_color = 0;
    char toast[32] = "";     /* short confirmation line (SAVED etc.) */
    double toast_t = 0.0;
    ItemStack cursor = { ITEM_NONE, 0 }; /* stack riding the mouse */
    int drag_from = -1;      /* slot the current mouse press began on,
                              * so a press-drag-release completes the
                              * move the way dragging should */
    int drag_lifted = 0;     /* that press lifted a whole stack into an
                              * empty hand, leaving its slot free to
                              * take the other item in a swap */
    double slash_t = 0.0;    /* melee slash-arc animation timer */
    double spawn_timer = 8.0; /* Minecraft-style trickle spawner */
    double poison_acc = 0.0; /* accumulates dt into 1-second venom ticks */
    int want_restart = 0;
    enum { GS_TITLE, GS_PLAY, GS_PAUSE } gstate = GS_TITLE;

    inventory_init(&player.inv); /* player_init leaves the pack alone */

    /* title screen first: the mouse stays free until the player
     * clicks to begin */
    SDL_SetRelativeMouseMode(SDL_FALSE);

    int running = 1;
    int show_minimap = cfg.show_minimap;
    int mouse_captured = 0;
    int palette_on = 1;

    /* High-resolution clock for frame-rate-independent movement:
     * everything below scales by dt (seconds since last frame). */
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last_time = SDL_GetPerformanceCounter();
    double fps_accum = 0.0;
    int fps_frames = 0;

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last_time) / (double)perf_freq;
        last_time = now;
        if (dt > 0.1) /* clamp huge steps (window drag, debugger pause) */
            dt = 0.1;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            /* the title and pause screens swallow gameplay input */
            if (gstate == GS_TITLE) {
                if (ev.type == SDL_QUIT)
                    running = 0;
                else if (ev.type == SDL_MOUSEBUTTONDOWN
                         || (ev.type == SDL_KEYDOWN
                             && ev.key.keysym.sym == SDLK_RETURN)) {
                    gstate = GS_PLAY;
                    mouse_captured = 1;
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                } else if (ev.type == SDL_KEYDOWN
                           && ev.key.keysym.sym == SDLK_ESCAPE)
                    running = 0;
                continue;
            }
            if (gstate == GS_PAUSE) {
                if (ev.type == SDL_QUIT)
                    running = 0;
                else if (ev.type == SDL_KEYDOWN) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        gstate = GS_PLAY;
                        mouse_captured = 1;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                    } else if (ev.key.keysym.sym == SDLK_q)
                        running = 0;
                    else if (ev.key.keysym.sym == SDLK_r) {
                        /* restarting lives ONLY here now — no more
                         * runs lost to a stray R mid-fight */
                        want_restart = 1;
                        gstate = GS_PLAY;
                        mouse_captured = 1;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                    }
                }
                continue;
            }
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    gstate = GS_PAUSE;
                    mouse_captured = 0;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                }
                else if (ev.key.keysym.sym == SDLK_h)
                    intro_t = 9.0;
                else if (ev.key.keysym.sym == SDLK_m)
                    show_minimap = !show_minimap;
                else if (ev.key.keysym.sym == SDLK_c)
                    show_controls = !show_controls;
                else if (ev.key.keysym.sym == SDLK_i) {
                    /* the inventory frees the mouse for clicking */
                    inv_open = !inv_open;
                    mouse_captured = !inv_open;
                    SDL_SetRelativeMouseMode(
                        inv_open ? SDL_FALSE : SDL_TRUE);
                    /* Leaving relative mode restores the pointer to
                     * wherever it sat before mouse-look began, which
                     * can be off the panel entirely. Put it in the
                     * middle of the grid so the screen always opens
                     * somewhere you can click. */
                    if (inv_open)
                        SDL_WarpMouseInWindow(window,
                            160 * cfg.window_scale,
                            100 * cfg.window_scale);
                    /* closing with a stack still on the cursor: it
                     * goes back in the pack, or falls at your feet */
                    if (!inv_open && cursor.count > 0) {
                        while (cursor.count > 0
                               && inventory_add(&player.inv,
                                                cursor.type, 1))
                            cursor.count--;
                        while (cursor.count-- > 0)
                            entlist_add(&ents, entity_make_item(
                                player.x, player.y, cursor.type));
                        cursor = (ItemStack){ ITEM_NONE, 0 };
                    }
                }
                else if (ev.key.keysym.sym == SDLK_v)
                    third_person = !third_person;
                else if (ev.key.keysym.sym == SDLK_p)
                    palette_on = !palette_on;
                else if (ev.key.keysym.sym == SDLK_F5) {
                    SaveData sd;
                    sd.seed = seed;
                    sd.floor_num = floor_num;
                    sd.hp = player.hp;
                    for (int i = 0; i < HOTBAR_SLOTS; i++)
                        sd.hotbar[i] = player.inv.hotbar[i];
                    for (int i = 0; i < STORE_SLOTS; i++)
                        sd.store[i] = player.inv.store[i];
                    sd.offhand = player.inv.offhand;
                    sd.selected = player.inv.selected;
                    adapt_export(sd.adapt);
                    snprintf(toast, sizeof toast,
                             save_write("kovil.sav", &sd) == 0
                                 ? "DESCENT SAVED"
                                 : "SAVE FAILED!");
                    toast_t = 2.0;
                }
                else if (ev.key.keysym.sym == SDLK_F9) {
                    SaveData sd;
                    if (save_read("kovil.sav", &sd) == 0) {
                        seed = sd.seed;
                        floor_num = sd.floor_num;
                        adapt_import(sd.adapt);
                        if (load_floor(&map, &ents, &player, &pz,
                                       seed, floor_num) != 0) {
                            fprintf(stderr, "map generation failed\n");
                            running = 0;
                            break;
                        }
                        player.hp = sd.hp;
                        for (int i = 0; i < HOTBAR_SLOTS; i++)
                            player.inv.hotbar[i] = sd.hotbar[i];
                        for (int i = 0; i < STORE_SLOTS; i++)
                            player.inv.store[i] = sd.store[i];
                        player.inv.offhand = sd.offhand;
                        player.inv.selected = sd.selected;
                        prev_hp = player.hp;
                        won = 0;
                        death_t = descend_t = 0.0;
                        dread = 0.0;
                        intro_t = 9.0;
                        snprintf(toast, sizeof toast,
                                 "DESCENT RESTORED");
                    } else {
                        snprintf(toast, sizeof toast, "NO SAVE FOUND");
                    }
                    toast_t = 2.0;
                }
                else if (ev.key.keysym.sym == SDLK_RETURN && won)
                    want_restart = 1; /* replay from the win screen */
                else if (ev.key.keysym.sym == SDLK_SPACE)
                    player_jump(&player);
                else if (ev.key.keysym.sym >= SDLK_1
                         && ev.key.keysym.sym <= SDLK_5) {
                    int n = (int)(ev.key.keysym.sym - SDLK_1);
                    if (inv_open) {
                        /* hover a slot, press a number: it swaps into
                         * that hotbar slot. No dragging needed at all,
                         * and it works even one-handed. */
                        int wx, wy;
                        SDL_GetMouseState(&wx, &wy);
                        int over = slot_index_at(&player.inv,
                                                 wx / cfg.window_scale,
                                                 wy / cfg.window_scale);
                        if (over >= 0) {
                            int ox, oy;
                            ItemStack *s =
                                slot_at(&player.inv, over, &ox, &oy);
                            ItemStack *h = &player.inv.hotbar[n];
                            if (s != h) {
                                ItemStack t = *s;
                                *s = *h;
                                *h = t;
                                audio_play(SND_PICKUP);
                            }
                        }
                    } else {
                        player.inv.selected = n;
                    }
                }
                else if (ev.key.keysym.sym == SDLK_e
                         && death_t <= 0.0 && descend_t <= 0.0
                         && !won && !inv_open) {
                    /* chests are opened here (the loot needs the
                     * inventory); everything else is the puzzles' */
                    int handled = 0;
                    for (int i = 0; i < ents.count; i++) {
                        Entity *e = &ents.items[i];
                        if (e->type != ENT_CHEST || e->as.chest.opened)
                            continue;
                        double dx = e->x - player.x;
                        double dy = e->y - player.y;
                        double d = SDL_sqrt(dx * dx + dy * dy);
                        if (d > 1.6 || d < 1e-6
                            || (dx * player.dirX + dy * player.dirY)
                                   / d < 0.6)
                            continue;
                        e->as.chest.opened = 1;
                        e->tex = TEX_CHEST_OPEN;
                        ItemType gift =
                            (ItemType)(ITEM_POT_HEAL
                                       + rng_range(&flick_rng, 3));
                        if (inventory_add(&player.inv, gift, 1)) {
                            snprintf(toast, sizeof toast, "FOUND %s",
                                     item_name(gift));
                        } else {
                            /* full pack: the potion waits beside
                             * the chest instead of vanishing */
                            entlist_add(&ents, entity_make_item(
                                e->x, e->y + 0.4, gift));
                            snprintf(toast, sizeof toast,
                                     "PACK FULL - IT FELL OUT");
                        }
                        toast_t = 2.5;
                        audio_play(SND_CHIME);
                        handled = 1;
                        break;
                    }
                    if (!handled)
                        puzzle_interact(&pz, &map, &ents, &player);
                }
                else if (ev.key.keysym.sym == SDLK_TAB) {
                    /* release/recapture the mouse without quitting */
                    mouse_captured = !mouse_captured;
                    SDL_SetRelativeMouseMode(
                        mouse_captured ? SDL_TRUE : SDL_FALSE);
                }
                break;
            case SDL_MOUSEMOTION:
                if (mouse_captured && !show_controls) {
                    player_rotate(&player, ev.motion.xrel * MOUSE_SENS
                                           * cfg.sensitivity);
                    /* mouse up = look up = horizon pushed down-screen */
                    player.pitch -= ev.motion.yrel * 0.4
                                    * cfg.sensitivity;
                    if (player.pitch > 85.0) player.pitch = 85.0;
                    if (player.pitch < -85.0) player.pitch = -85.0;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                /* Press-drag-release. The press already lifted the
                 * stack onto the cursor. Releasing over the SAME slot
                 * leaves it held, which keeps plain click-then-click
                 * working. Releasing anywhere else finishes a drag: a
                 * clean lift moves or swaps through inventory_drop;
                 * a press that was itself a swap acts as a click. */
                if (inv_open && drag_from >= 0 && cursor.count > 0
                    && ev.button.button == SDL_BUTTON_LEFT) {
                    int mx = ev.button.x / cfg.window_scale;
                    int my = ev.button.y / cfg.window_scale;
                    int over = slot_index_at(&player.inv, mx, my);
                    if (over != drag_from) {
                        int dropped = drag_lifted
                            && inventory_drop(&player.inv, &cursor,
                                              drag_from, over);
                        if (!dropped && over >= 0)
                            inventory_click(&player.inv, &cursor, mx, my,
                                            SDL_BUTTON_LEFT, 0);
                    }
                }
                drag_from = -1;
                drag_lifted = 0;
                break;
            case SDL_MOUSEWHEEL:
                if (ev.wheel.y > 0)
                    player.inv.selected =
                        (player.inv.selected + HOTBAR_SLOTS - 1)
                        % HOTBAR_SLOTS;
                else if (ev.wheel.y < 0)
                    player.inv.selected =
                        (player.inv.selected + 1) % HOTBAR_SLOTS;
                break;
            case SDL_MOUSEBUTTONDOWN: {
                if (inv_open) {
                    /* window coords -> framebuffer coords */
                    int shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    int hand_was_empty = cursor.count == 0;
                    drag_from = inventory_click(
                        &player.inv, &cursor,
                        ev.button.x / cfg.window_scale,
                        ev.button.y / cfg.window_scale,
                        ev.button.button, shift);
                    /* only a plain left press that picked a stack up
                     * into an empty hand leaves its slot empty — the
                     * one case where a drop can send the other home */
                    drag_lifted = drag_from >= 0 && hand_was_empty
                                  && !shift && cursor.count > 0
                                  && ev.button.button == SDL_BUTTON_LEFT;
                    break;
                }
                if (!mouse_captured || death_t > 0.0
                    || descend_t > 0.0 || won || show_controls)
                    break;

                /* right click always hurls flame; left click uses
                 * whatever the hand holds — Minecraft rules */
                if (ev.button.button == SDL_BUTTON_RIGHT) {
                    if (flame_cd <= 0.0) {
                        entlist_add(&ents, entity_make_flame(
                            player.x + player.dirX * 0.5,
                            player.y + player.dirY * 0.5,
                            player.dirX, player.dirY));
                        audio_play(SND_FLAME);
                        flame_cd = 0.9;
                        attack_anim = 0.25;
                    }
                    break;
                }
                if (ev.button.button != SDL_BUTTON_LEFT)
                    break;

                ItemStack *held = inventory_held(&player.inv);
                switch (held->count > 0 ? held->type : ITEM_NONE) {
                case ITEM_VEL:
                    if (melee_cd <= 0.0) {
                        swing_melee(&ents, &player, 2, 1.8);
                        melee_cd = 0.5;
                        attack_anim = 0.25;
                        slash_t = 0.22;
                    }
                    break;
                case ITEM_SWORD:
                    if (melee_cd <= 0.0) {
                        swing_melee(&ents, &player, 4, 2.1);
                        melee_cd = 0.6;
                        attack_anim = 0.25;
                        slash_t = 0.22;
                    }
                    break;
                case ITEM_BOW:
                    if (flame_cd <= 0.0) {
                        entlist_add(&ents, entity_make_arrow(
                            player.x + player.dirX * 0.5,
                            player.y + player.dirY * 0.5,
                            player.dirX, player.dirY, 0));
                        audio_play(SND_VEL_SWING);
                        flame_cd = 0.7;
                        attack_anim = 0.2;
                    }
                    break;
                case ITEM_FLAME:
                    if (flame_cd <= 0.0) {
                        entlist_add(&ents, entity_make_flame(
                            player.x + player.dirX * 0.5,
                            player.y + player.dirY * 0.5,
                            player.dirX, player.dirY));
                        audio_play(SND_FLAME);
                        flame_cd = 0.9;
                        attack_anim = 0.25;
                    }
                    break;
                case ITEM_POT_HEAL:
                case ITEM_POT_STR:
                case ITEM_POT_SPD: {
                    ItemType t = held->type;
                    if (t == ITEM_POT_HEAL) {
                        player.hp += 40;
                        if (player.hp > PLAYER_MAX_HP)
                            player.hp = PLAYER_MAX_HP;
                        prev_hp = player.hp; /* no hurt-flash */
                        drink_color = COLOR_RGB(232, 59, 59);
                    } else if (t == ITEM_POT_STR) {
                        player.buff_str_t = 20.0;
                        drink_color = COLOR_RGB(251, 107, 29);
                    } else {
                        player.buff_spd_t = 20.0;
                        drink_color = COLOR_RGB(48, 225, 185);
                    }
                    drink_icon = item_texture(t);
                    drink_t = 0.8;
                    audio_play(SND_DRINK);
                    if (--held->count <= 0)
                        held->type = ITEM_NONE;
                    break;
                }
                default:
                    break; /* shield blocks passively; empty hands
                            * swing at nothing */
                }
                break;
            }
            default:
                break;
            }
        }

        /* restart the run (pause-menu R, or Enter on the win screen):
         * a fresh temple from floor 1, fresh pack, and the temple
         * forgets what it learned about you too */
        if (want_restart) {
            want_restart = 0;
            seed = seed * 1664525u + 1013904223u;
            floor_num = 1;
            won = 0;
            descend_t = 0.0;
            inventory_init(&player.inv);
            cursor = (ItemStack){ ITEM_NONE, 0 };
            adapt_reset();
            if (load_floor(&map, &ents, &player, &pz, seed,
                           floor_num) != 0) {
                fprintf(stderr, "map generation failed\n");
                running = 0;
            }
            prev_hp = player.hp;
            death_t = 0.0;
            dread = 0.0;
            intro_t = 9.0;
            spawn_timer = 8.0;
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        int overlay = inv_open || show_controls; /* freezes the world */

        /* Minecraft controls: Ctrl = sprint, Shift = sneak; the
         * swiftness potion multiplies whatever you're doing */
        double speed = MOVE_SPEED;
        if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])
            speed = 5.0;
        else if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
            speed = 1.5;
        if (player.buff_spd_t > 0.0)
            speed *= 1.5;
        double step = speed * dt;
        double dx = 0.0, dy = 0.0;

        if (!overlay) {
            if (keys[SDL_SCANCODE_W]) { dx += player.dirX; dy += player.dirY; }
            if (keys[SDL_SCANCODE_S]) { dx -= player.dirX; dy -= player.dirY; }
            /* strafe direction = facing rotated 90 degrees */
            if (keys[SDL_SCANCODE_A]) { dx += player.dirY; dy -= player.dirX; }
            if (keys[SDL_SCANCODE_D]) { dx -= player.dirY; dy += player.dirX; }
            if (keys[SDL_SCANCODE_LEFT])
                player_rotate(&player, -KEY_TURN * dt);
            if (keys[SDL_SCANCODE_RIGHT])
                player_rotate(&player, KEY_TURN * dt);
        }

        if (dx != 0.0 || dy != 0.0) {
            /* normalise so moving diagonally isn't faster */
            double len = SDL_sqrt(dx * dx + dy * dy);
            player_move(&player, &map, dx / len * step, dy / len * step);
            bob_phase += dt * 9.0; /* weapon sway while walking */
        }
        player_physics(&player, dt);

        if (melee_cd > 0.0) melee_cd -= dt;
        if (flame_cd > 0.0) flame_cd -= dt;
        if (attack_anim > 0.0) attack_anim -= dt;
        if (slash_t > 0.0) slash_t -= dt;
        if (drink_t > 0.0) drink_t -= dt;
        if (toast_t > 0.0) toast_t -= dt;
        if (player.buff_str_t > 0.0) player.buff_str_t -= dt;
        if (player.buff_spd_t > 0.0) player.buff_spd_t -= dt;

        /* Blocking, Minecraft-style: crouch (Shift) with a shield in
         * either hand — the offhand slot exists exactly so a weapon
         * can stay in the right hand while the left holds the shield. */
        {
            const ItemStack *held = inventory_held(&player.inv);
            int has_shield =
                (player.inv.offhand.count > 0
                 && player.inv.offhand.type == ITEM_SHIELD)
                || (held->count > 0 && held->type == ITEM_SHIELD);
            int sneaking = keys[SDL_SCANCODE_LSHIFT]
                        || keys[SDL_SCANCODE_RSHIFT];
            player.blocking = has_shield && sneaking && !overlay;
        }

        int playing = (gstate == GS_PLAY);

        World world = { &map, &player, &ents, dt };
        if (playing && death_t <= 0.0 && descend_t <= 0.0 && !won
            && !overlay) {
            entities_update(&ents, &world);
            particles_update(dt);

            /* walk over dropped loot to pocket it */
            for (int i = 0; i < ents.count; i++) {
                Entity *e = &ents.items[i];
                if (e->type != ENT_ITEM)
                    continue;
                double idx = e->x - player.x, idy = e->y - player.y;
                if (idx * idx + idy * idy < 0.7 * 0.7
                    && inventory_add(&player.inv,
                                     (ItemType)e->as.item.what, 1)) {
                    snprintf(toast, sizeof toast, "PICKED UP %s",
                             item_name((ItemType)e->as.item.what));
                    toast_t = 2.0;
                    audio_play(SND_PICKUP);
                    e->remove = 1;
                    /* sweep happens next entities_update; hide it
                     * from this frame's render too */
                    ents.items[i] = ents.items[--ents.count];
                    i--;
                }
            }
            /* Naga venom: one tick per second, a tenth of a heart
             * each (1 hp of the 10-hp hearts). Like Minecraft's
             * poison it gnaws but never lands the killing blow. */
            if (player.poison_t > 0.0) {
                player.poison_t -= dt;
                poison_acc += dt;
                if (poison_acc >= 1.0) {
                    poison_acc -= 1.0;
                    if (player.hp > 1) {
                        player.hp -= 1;
                        prev_hp = player.hp; /* no hurt-shake per tick */
                    }
                }
            } else {
                poison_acc = 0.0;
            }

            /* Minecraft-style trickle spawning: every few seconds,
             * if the floor is below its population cap, one more
             * guardian condenses out of the dark — always dormant,
             * always well away from the player and out of sight. */
            spawn_timer -= dt;
            if (spawn_timer <= 0.0) {
                spawn_timer = 7.0;
                int alive = 0;
                for (int i = 0; i < ents.count; i++) {
                    const Entity *e = &ents.items[i];
                    if (ENTITY_IS_ENEMY(e) && e->type != ENT_BOSS
                        && e->state != ST_DYING)
                        alive++;
                }
                if (alive < 7 + floor_num * 2) {
                    for (int t = 0; t < 12; t++) { /* placement tries */
                        int sx = rng_range(&flick_rng, map.w);
                        int sy = rng_range(&flick_rng, map.h);
                        double ddx = sx + 0.5 - player.x;
                        double ddy = sy + 0.5 - player.y;
                        double d2 = ddx * ddx + ddy * ddy;
                        if (!map_walkable(&map, sx, sy)
                            || d2 < 9.0 * 9.0 || d2 > 20.0 * 20.0
                            || map_los(&map, sx + 0.5, sy + 0.5,
                                       player.x, player.y))
                            continue;
                        double ex = sx + 0.5, ey = sy + 0.5;
                        switch (rng_range(&flick_rng, 5)) {
                        case 0: entlist_add(&ents,
                                    entity_make_dwarapalaka(ex, ey));
                                break;
                        case 1: entlist_add(&ents,
                                    entity_make_yali(ex, ey));
                                break;
                        case 2: entlist_add(&ents,
                                    entity_make_naga(ex, ey));
                                break;
                        case 3: entlist_add(&ents,
                                    entity_make_archer(ex, ey));
                                break;
                        default: entlist_add(&ents,
                                    entity_make_knight(ex, ey));
                        }
                        break;
                    }
                }
            }
        }
        map_reveal(&map, player.x, player.y, 7.5);
        if (intro_t > 0.0 && playing && !overlay)
            intro_t -= dt;

        /* floor completed: gold fade out, swap floors, fade back in */
        if (pz.shrine_lit && descend_t <= 0.0) {
            descend_t = 2.4;
            descended = 0;
        }
        if (descend_t > 0.0) {
            descend_t -= dt;
            if (!descended && descend_t <= 1.2) {
                descended = 1;
                if (floor_num >= NUM_FLOORS) {
                    won = 1; /* every shrine relit: the keeper's work
                              * is done */
                    descend_t = 0.0;
                } else {
                    floor_num++;
                    seed = seed * 1664525u + 1013904223u;
                    if (load_floor(&map, &ents, &player, &pz, seed,
                                   floor_num) != 0) {
                        fprintf(stderr, "map generation failed\n");
                        running = 0;
                    }
                    prev_hp = player.hp;
                    dread = 0.0;
                    intro_t = 9.0;
                }
            }
        }

        /* damage feedback + death handling */
        if (player.hp < prev_hp) {
            hurt = 200.0;
            shake = 6.0; /* the hit rattles the view */
        }
        prev_hp = player.hp;
        if (shake > 0.0) {
            shake -= dt * 14.0;
            if (shake < 0.0)
                shake = 0.0;
        }
        if (hurt > 0.0)
            hurt -= dt * 500.0;
        if (player.hp <= 0 && death_t <= 0.0)
            death_t = 1.6; /* seconds of fade before the reset */
        if (death_t > 0.0) {
            death_t -= dt;
            if (death_t <= 0.0) {
                /* death resets the whole floor: same seed regenerates
                 * it exactly — doors reseal, statues return to their
                 * posts, lamps go cold */
                if (load_floor(&map, &ents, &player, &pz, seed,
                               floor_num) != 0) {
                    fprintf(stderr, "map generation failed\n");
                    running = 0;
                }
                prev_hp = player.hp;
                dread = 0.0;
            }
        }

        /* Ease the light level toward a randomly wandering target:
         * fast enough to gutter, slow enough not to strobe. */
        double target = 0.93 + rng_range(&flick_rng, 70) / 1000.0;
        flick += (target - flick) * (dt * 8.0 > 1.0 ? 1.0 : dt * 8.0);
        raycast_set_flicker(flick);
        raycast_set_time(SDL_GetTicks() / 1000.0);

        /* Dread: strongest red-edge pull from any awake guardian that
         * can currently see the player, fading in over ~9 tiles. */
        double dread_target = 0.0;
        for (int i = 0; i < ents.count; i++) {
            const Entity *e = &ents.items[i];
            if (!ENTITY_IS_ENEMY(e))
                continue;
            if (e->state == ST_DORMANT || e->state == ST_DYING)
                continue;
            double dx = e->x - player.x, dy = e->y - player.y;
            double d = SDL_sqrt(dx * dx + dy * dy);
            double t = 1.0 - d / 9.0;
            if (t > dread_target
                && map_los(&map, e->x, e->y, player.x, player.y))
                dread_target = t;
        }
        dread += (dread_target * 200.0 - dread)
                 * (dt * 3.0 > 1.0 ? 1.0 : dt * 3.0);

        /* The camera: the player's own eyes, or — in third person —
         * a viewpoint pulled back along the facing line (stepping
         * closer whenever a wall would block it), with the keeper
         * drawn into the world as a billboard. */
        Player cam = player;
        if (third_person) {
            double back = 1.7;
            while (back > 0.2) {
                double cx = player.x - player.dirX * back;
                double cy = player.y - player.dirY * back;
                if (map_walkable(&map, (int)cx, (int)cy)
                    && map_los(&map, player.x, player.y, cx, cy)) {
                    cam.x = cx;
                    cam.y = cy;
                    break;
                }
                back -= 0.15;
            }
            cam.z += 0.28; /* over-the-shoulder lift; the raised
                            * WALL_H ceilings leave it headroom */
        }
        /* screen shake: jolt the camera pitch for this frame only */
        if (shake > 0.0)
            cam.pitch += (rng_range(&flick_rng, 200) / 100.0 - 1.0)
                         * shake;

        raycast_render(fb, &map, &cam);
        if (third_person) {
            /* borrow a slot in the entity list for the avatar, then
             * give it straight back — it must never be updated */
            Entity av = {0};
            av.type = ENT_ITEM;
            av.x = player.x;
            av.y = player.y;
            av.tex = TEX_PLAYER;
            av.widthm = 0.9;
            av.anim = bob_phase * 0.55; /* walk cycle from movement */
            if (attack_anim > 0.0) {
                /* the keeper lunges into the blow */
                double a = SDL_sin(attack_anim / 0.25 * PI);
                av.x += player.dirX * 0.22 * a;
                av.y += player.dirY * 0.22 * a;
            }
            if (entlist_add(&ents, av)) {
                raycast_sprites(fb, &cam, &ents);
                ents.count--;
            } else {
                raycast_sprites(fb, &cam, &ents);
            }
        } else {
            raycast_sprites(fb, &cam, &ents);
        }
        particles_render(fb, &cam);

        /* first-person hands: the held item bobs while walking and
         * thrusts on attack (hidden in third person) */
        if (!third_person) {
            const ItemStack *held = inventory_held(&player.inv);
            if (held->count > 0) {
                int wx = 196 + (int)(SDL_sin(bob_phase) * 5.0);
                int wy = 92 + (int)(SDL_fabs(SDL_cos(bob_phase)) * 4.0);
                if (attack_anim > 0.0) {
                    double a = SDL_sin(attack_anim / 0.25 * PI);
                    wx -= (int)(a * 26.0);
                    wy -= (int)(a * 18.0);
                }
                fb_blit_tex(fb, texture_pixels(item_texture(held->type)),
                            wx, wy, 2);
            }

            /* the slash arc: a bright crescent sweeping across the
             * view while a melee swing is live — the "animation" of
             * the blow itself, over and above the weapon thrust */
            if (slash_t > 0.0) {
                double a = 1.0 - slash_t / 0.22; /* 0 -> 1 sweep */
                double ang = -1.1 + a * 2.0;     /* radians */
                for (int k = 0; k < 3; k++) {
                    double aa = ang - k * 0.18;
                    for (int r = 30; r < 52; r += 2) {
                        int px = 160 + (int)(SDL_sin(aa) * r);
                        int py = 100 - (int)(SDL_cos(aa) * r * 0.6);
                        fb_fill_rect(fb, px, py, 2, 2,
                                     k == 0 ? COLOR_RGB(255, 255, 255)
                                            : COLOR_RGB(255, 240, 150));
                    }
                }
            }

            /* the raised shield fills the left hand while sneaking */
            if (player.blocking)
                fb_blit_tex(fb, texture_pixels(TEX_SHIELD), 30, 104, 2);
        }

        /* drinking: the bottle rises to the lips, the world tints */
        if (drink_t > 0.0) {
            double f = drink_t / 0.8; /* 1 -> 0 */
            fb_blit_tex(fb, texture_pixels(drink_icon),
                        150, 60 + (int)(f * 90.0), 1);
            fb_flash(fb, drink_color, (uint8_t)(60.0 * (1.0 - f)));
        }

        fb_dread(fb, (uint8_t)dread);
        /* a faint sickly cast over everything while venom runs */
        if (player.poison_t > 0.0)
            fb_flash(fb, COLOR_RGB(117, 167, 67), 26);
        if (hurt > 0.0)
            fb_flash(fb, COLOR_RGB(180, 30, 20), (uint8_t)hurt);
        if (death_t > 0.0) {
            double f = 1.0 - death_t / 1.6;
            fb_flash(fb, COLOR_RGB(0, 0, 0),
                     (uint8_t)(f * 255.0));
        }

        draw_hud(fb, &player);

        /* ---- text overlays ---- */

        /* contextual instruction for whatever E would do right now */
        if (playing && !overlay && !won && death_t <= 0.0) {
            const char *pr = puzzle_prompt(&pz, &map, &ents, &player);
            if (pr)
                draw_centered(fb, 112, 1, COLOR_RGB(255, 240, 150),
                              pr);
        }

        /* toast line: saves, pickups, chest finds */
        if (toast_t > 0.0)
            draw_centered(fb, 152, 1, COLOR_RGB(255, 255, 255), toast);

        /* boss health bar, Ender-Dragon style, once the King wakes */
        for (int i = 0; i < ents.count; i++) {
            const Entity *e = &ents.items[i];
            if (e->type != ENT_BOSS || e->state == ST_DORMANT
                || e->state == ST_DYING)
                continue;
            draw_centered(fb, 6, 1, COLOR_RGB(240, 79, 120),
                          "THE ASURA KING");
            fb_fill_rect(fb, 99, 15, 122, 7, COLOR_RGB(20, 16, 14));
            fb_fill_rect(fb, 100, 16, 120 * e->hp / e->maxhp, 5,
                         COLOR_RGB(195, 36, 84));
            break;
        }

        /* objective tracker, top right */
        if (gstate == GS_PLAY && !won) {
            char line[40];
            int ty = 4;
            uint32_t done = COLOR_RGB(145, 219, 105);
            uint32_t todo = COLOR_RGB(249, 194, 43);
            if (pz.lamps_done)
                snprintf(line, sizeof line, "LAMPS LIT");
            else
                snprintf(line, sizeof line, "LAMPS %d/%d",
                         pz.lamps_next - 1, pz.lamps_total);
            text_draw_shadow(fb, 316 - text_width(line, 1), ty, 1,
                             pz.lamps_done ? done : todo, line);
            ty += 9;
            if (map.nrotors > 0) {
                snprintf(line, sizeof line, pz.rotors_done
                         ? "PILLARS ALIGNED" : "ALIGN PILLARS");
                text_draw_shadow(fb, 316 - text_width(line, 1), ty, 1,
                                 pz.rotors_done ? done : todo, line);
                ty += 9;
            }
            snprintf(line, sizeof line, pz.doors_open
                     ? "SANCTUM OPEN" : "SANCTUM SEALED");
            text_draw_shadow(fb, 316 - text_width(line, 1), ty, 1,
                             pz.doors_open ? done : todo, line);
            ty += 9;
            if (floor_num == NUM_FLOORS) {
                int boss_alive = 0;
                for (int i = 0; i < ents.count; i++)
                    if (ents.items[i].type == ENT_BOSS
                        && ents.items[i].state != ST_DYING)
                        boss_alive = 1;
                snprintf(line, sizeof line, boss_alive
                         ? "THE KING LIVES" : "THE KING HAS FALLEN");
                text_draw_shadow(fb, 316 - text_width(line, 1), ty, 1,
                                 boss_alive ? COLOR_RGB(240, 79, 120)
                                            : done, line);
            }
        }

        /* floor-start instructions (H re-shows them) */
        if (intro_t > 0.0 && gstate == GS_PLAY && !won) {
            char head[48];
            snprintf(head, sizeof head, "FLOOR %d - %s", floor_num,
                     floor_names[floor_num - 1]);
            draw_centered(fb, 30, 2, COLOR_RGB(255, 240, 150), head);
            draw_centered(fb, 50, 1, COLOR_RGB(255, 255, 255),
                          "LIGHT THE 3 MARKED LAMPS IN ORDER");
            int ty = 60;
            if (map.nrotors > 0) {
                draw_centered(fb, ty, 1, COLOR_RGB(255, 255, 255),
                              "TURN THE ARROW PILLARS TO AGREE");
                ty += 10;
            }
            draw_centered(fb, ty, 1, COLOR_RGB(255, 255, 255),
                          "ENTER THE SANCTUM - LIGHT THE SHRINE");
            ty += 10;
            if (floor_num == NUM_FLOORS) {
                draw_centered(fb, ty, 1, COLOR_RGB(240, 79, 120),
                              "BUT FIRST: SLAY THE ASURA KING");
                ty += 10;
            }
            ty += 10;
            draw_centered(fb, ty, 1, COLOR_RGB(255, 255, 255),
                          "OPEN CHESTS - DRINK WHAT YOU FIND");
            draw_centered(fb, ty + 14, 1, COLOR_RGB(155, 171, 178),
                          "C SHOWS ALL CONTROLS");
        }

        /* the controls panel, top-middle, over a dimmed world */
        if (show_controls) {
            fb_fill_rect(fb, 58, 6, 204, 142, COLOR_RGB(62, 53, 70));
            fb_fill_rect(fb, 60, 8, 200, 138, COLOR_RGB(24, 18, 14));
            draw_centered(fb, 12, 1, COLOR_RGB(249, 194, 43),
                          "CONTROLS");
            static const char *lines[] = {
                "WASD MOVE - MOUSE LOOK - SPACE JUMP",
                "CTRL SPRINT - SHIFT SNEAK",
                "SHIFT + SHIELD IN A HAND = BLOCK",
                "LMB USE ITEM - RMB THROW FLAME",
                "1-5 OR WHEEL: HOTBAR - I: INVENTORY",
                "BAG: DRAG ONTO A SLOT TO SWAP",
                "BAG: SHIFT-CLICK SENDS TO HOTBAR",
                "BAG: RMB SPLITS - 1-5 SWAPS SLOT",
                "E INTERACT - V VIEW - M MAP",
                "F5 SAVE - F9 LOAD - H OBJECTIVES",
                "ESC PAUSE - C CLOSES THIS PANEL",
            };
            for (int i = 0; i < 11; i++)
                draw_centered(fb, 24 + i * 11, 1,
                              COLOR_RGB(222, 210, 190), lines[i]);
        }

        if (inv_open) {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            draw_inventory(fb, &player.inv, &cursor,
                           mx / cfg.window_scale,
                           my / cfg.window_scale);
        }

        /* gold wash while descending between floors */
        if (descend_t > 0.0) {
            double f = descend_t > 1.2 ? (2.4 - descend_t) / 1.2
                                       : descend_t / 1.2;
            fb_flash(fb, COLOR_RGB(249, 194, 43),
                     (uint8_t)(f * 255.0));
        }
        /* victory: the lit shrine held in gold light */
        if (won) {
            fb_flash(fb, COLOR_RGB(249, 194, 43), 140);
            fb_blit_tex(fb, texture_pixels(TEX_SHRINE_ON), 96, 40, 2);
            draw_centered(fb, 14, 2, COLOR_RGB(255, 240, 150),
                          "EVERY LAMP IS LIT");
            draw_centered(fb, 178, 1, COLOR_RGB(255, 255, 255),
                          "THE TEMPLE SLEEPS - ENTER TO REPLAY");
        }

        /* title screen */
        if (gstate == GS_TITLE) {
            fb_flash(fb, COLOR_RGB(20, 16, 14), 150);
            draw_centered(fb, 52, 4, COLOR_RGB(249, 194, 43), "KOVIL");
            draw_centered(fb, 96, 1, COLOR_RGB(222, 210, 190),
                          "TEMPLE OF THE LOST LAMPS");
            if ((SDL_GetTicks() / 500) & 1)
                draw_centered(fb, 130, 1, COLOR_RGB(255, 255, 255),
                              "CLICK TO BEGIN");
        }

        /* pause screen */
        if (gstate == GS_PAUSE) {
            fb_flash(fb, COLOR_RGB(20, 16, 14), 130);
            draw_centered(fb, 70, 3, COLOR_RGB(255, 255, 255),
                          "PAUSED");
            draw_centered(fb, 110, 1, COLOR_RGB(222, 210, 190),
                          "ESC: RESUME   R: RESTART RUN   Q: QUIT");
        }

        if (show_minimap && !inv_open)
            draw_minimap(fb, &map, &player, &ents);

        /* snap the finished frame to the 64-colour palette */
        if (palette_on)
            palette_quantize_frame(fb);

        SDL_UpdateTexture(screen, NULL, fb->pixels,
                          FB_WIDTH * (int)sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, screen, NULL, NULL);
        SDL_RenderPresent(renderer);

        /* Rolling FPS readout in the title bar, refreshed twice a second. */
        fps_accum += dt;
        fps_frames++;
        if (fps_accum >= 0.5) {
            char title[64];
            if (won)
                snprintf(title, sizeof title,
                         "Kovil — the lamps are lit — Enter to replay");
            else
                snprintf(title, sizeof title,
                         "Kovil — floor %d/%d — seed %u — %.0f FPS",
                         floor_num, NUM_FLOORS, seed,
                         fps_frames / fps_accum);
            SDL_SetWindowTitle(window, title);
            fps_accum = 0.0;
            fps_frames = 0;
        }
    }

    entlist_free(&ents);
    pathfind_shutdown();
    map_free(&map);
    audio_shutdown();
    textures_shutdown();
    fb_destroy(fb);
    SDL_DestroyTexture(screen);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
