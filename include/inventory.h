#ifndef KOVIL_INVENTORY_H
#define KOVIL_INVENTORY_H

/* Minecraft-style inventory: a hotbar of quick slots (1-5 keys, mouse
 * wheel) plus a storage grid opened with I. Weapons occupy a slot
 * each; potions stack. */

typedef enum {
    ITEM_NONE = 0,
    ITEM_VEL,      /* the keeper's leaf-bladed spear (melee) */
    ITEM_FLAME,    /* consecrated flame (thrown, infinite) */
    ITEM_SWORD,    /* asura champion's blade: harder, longer melee */
    ITEM_BOW,      /* asura bowman's bow: fast arrows */
    ITEM_SHIELD,   /* held: halves incoming strikes, but no attacking */
    ITEM_POT_HEAL, /* restores 40 health instantly */
    ITEM_POT_STR,  /* double damage for 20 seconds */
    ITEM_POT_SPD,  /* half again as fast for 20 seconds */
    ITEM_TYPE_COUNT
} ItemType;

typedef struct {
    ItemType type;
    int count; /* 0 means the slot is empty regardless of type */
} ItemStack;

#define HOTBAR_SLOTS 5
#define STORE_SLOTS 10 /* the 2x5 grid behind the I key */
#define STACK_MAX 5    /* how many of a stackable item fit one slot */

typedef struct {
    ItemStack hotbar[HOTBAR_SLOTS];
    ItemStack store[STORE_SLOTS];
    ItemStack offhand; /* the left hand: a shield here blocks while
                        * sneaking, leaving the right hand free */
    int selected;      /* 0..HOTBAR_SLOTS-1 */
} Inventory;

/* Starting kit: vel in slot 1, flame in slot 2. */
void inventory_init(Inventory *inv);

/* Add an item, stacking potions, filling the hotbar before storage.
 * Returns 1 if it fitted, 0 if every slot was full. */
int inventory_add(Inventory *inv, ItemType type, int count);

/* The stack under the current hotbar selection. */
ItemStack *inventory_held(Inventory *inv);

/* Does any slot hold this item? (shield checks, UI) */
int inventory_has(const Inventory *inv, ItemType type);

/* Do several of this item share one slot? Potions stack, weapons
 * don't. Exported because the inventory SCREEN needs the same rule
 * the pickup code uses — two copies of it would drift apart. */
int item_stacks(ItemType type);

/* Short display name for prompts and the inventory screen. */
const char *item_name(ItemType type);

#endif
