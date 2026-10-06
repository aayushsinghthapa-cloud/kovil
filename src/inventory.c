#include "inventory.h"

#include <stddef.h>

/* Potions stack to STACK_MAX; weapons are one per slot, and a
 * duplicate weapon simply doesn't fit (you only have two hands). */
int item_stacks(ItemType t)
{
    return t == ITEM_POT_HEAL || t == ITEM_POT_STR || t == ITEM_POT_SPD;
}

void inventory_init(Inventory *inv)
{
    for (int i = 0; i < HOTBAR_SLOTS; i++)
        inv->hotbar[i] = (ItemStack){ ITEM_NONE, 0 };
    for (int i = 0; i < STORE_SLOTS; i++)
        inv->store[i] = (ItemStack){ ITEM_NONE, 0 };
    inv->hotbar[0] = (ItemStack){ ITEM_VEL, 1 };
    inv->hotbar[1] = (ItemStack){ ITEM_FLAME, 1 };
    inv->offhand = (ItemStack){ ITEM_NONE, 0 };
    inv->selected = 0;
}

static int try_slot(ItemStack *s, ItemType type, int count)
{
    if (item_stacks(type) && s->count > 0 && s->type == type
        && s->count < STACK_MAX) {
        s->count += count;
        if (s->count > STACK_MAX)
            s->count = STACK_MAX;
        return 1;
    }
    if (s->count == 0) {
        s->type = type;
        s->count = count;
        return 1;
    }
    return 0;
}

int inventory_add(Inventory *inv, ItemType type, int count)
{
    /* a second copy of a weapon is worthless: refuse it so drops
     * stay meaningful */
    if (!item_stacks(type) && inventory_has(inv, type))
        return 0;

    /* two passes: merge into an existing stack anywhere first, then
     * take the first empty slot, hotbar before storage */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < HOTBAR_SLOTS; i++) {
            ItemStack *s = &inv->hotbar[i];
            int mergeable = item_stacks(type) && s->count > 0
                            && s->type == type;
            if ((pass == 0 ? mergeable : s->count == 0)
                && try_slot(s, type, count))
                return 1;
        }
        for (int i = 0; i < STORE_SLOTS; i++) {
            ItemStack *s = &inv->store[i];
            int mergeable = item_stacks(type) && s->count > 0
                            && s->type == type;
            if ((pass == 0 ? mergeable : s->count == 0)
                && try_slot(s, type, count))
                return 1;
        }
    }
    return 0;
}

ItemStack *inventory_held(Inventory *inv)
{
    return &inv->hotbar[inv->selected];
}

int inventory_has(const Inventory *inv, ItemType type)
{
    if (inv->offhand.count > 0 && inv->offhand.type == type)
        return 1;
    for (int i = 0; i < HOTBAR_SLOTS; i++)
        if (inv->hotbar[i].count > 0 && inv->hotbar[i].type == type)
            return 1;
    for (int i = 0; i < STORE_SLOTS; i++)
        if (inv->store[i].count > 0 && inv->store[i].type == type)
            return 1;
    return 0;
}

const char *item_name(ItemType type)
{
    switch (type) {
    case ITEM_VEL:      return "VEL";
    case ITEM_FLAME:    return "FLAME";
    case ITEM_SWORD:    return "ASURA SWORD";
    case ITEM_BOW:      return "ASURA BOW";
    case ITEM_SHIELD:   return "SHIELD";
    case ITEM_POT_HEAL: return "HEALING POTION";
    case ITEM_POT_STR:  return "STRENGTH POTION";
    case ITEM_POT_SPD:  return "SWIFTNESS POTION";
    default:            return "";
    }
}
