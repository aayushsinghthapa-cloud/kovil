#include "save.h"

#include <stdio.h>
#include <string.h>

/* "KVL1" little-endian; bump SAVE_VERSION whenever SaveData changes
 * so an old file is refused instead of misread as garbage. */
#define SAVE_MAGIC 0x314C564Bu
#define SAVE_VERSION 3u /* v3 added the offhand (shield) slot */

int save_write(const char *path, const SaveData *sd)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    uint32_t magic = SAVE_MAGIC, version = SAVE_VERSION;
    int ok = fwrite(&magic, sizeof magic, 1, f) == 1
          && fwrite(&version, sizeof version, 1, f) == 1
          && fwrite(sd, sizeof *sd, 1, f) == 1;

    /* fclose can fail too (disk full at flush time); count it */
    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

int save_read(const char *path, SaveData *sd)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    uint32_t magic = 0, version = 0;
    SaveData tmp;
    int ok = fread(&magic, sizeof magic, 1, f) == 1
          && fread(&version, sizeof version, 1, f) == 1
          && magic == SAVE_MAGIC
          && version == SAVE_VERSION
          && fread(&tmp, sizeof tmp, 1, f) == 1;
    fclose(f);

    if (!ok)
        return -1;
    /* only overwrite the caller's data once EVERYTHING validated */
    memcpy(sd, &tmp, sizeof tmp);
    return 0;
}
