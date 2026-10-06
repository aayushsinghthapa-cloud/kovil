#ifndef KOVIL_SAVE_H
#define KOVIL_SAVE_H

#include <stdint.h>

#include "player.h"

/* Binary save format: magic number + version header, then fixed-width
 * fields. Saving records the DESCENT — floor, seed, health, inventory
 * and the temple's adaptation memory. Loading regenerates the floor
 * from its seed (the same seed always rebuilds the same temple), so
 * mid-floor puzzle progress deliberately resets: the temple restores
 * itself while the keeper rests. */

typedef struct {
    uint32_t seed;
    int32_t floor_num;
    int32_t hp;
    ItemStack hotbar[HOTBAR_SLOTS];
    ItemStack store[STORE_SLOTS];
    ItemStack offhand;
    int32_t selected;
    int32_t adapt[16]; /* ADAPT_TYPES * 2 */
} SaveData;

/* Both return 0 on success, -1 on any I/O or format error (a corrupt
 * or wrong-version file is refused, never half-loaded). */
int save_write(const char *path, const SaveData *sd);
int save_read(const char *path, SaveData *sd);

#endif
