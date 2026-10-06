#ifndef KOVIL_TEXTURE_H
#define KOVIL_TEXTURE_H

#include <stdint.h>

/* All textures are 64x64. Power-of-two size means wrapping a texture
 * coordinate is a single bitwise AND with 63 instead of a modulo. */
#define TEX_SIZE 64
#define TEX_MASK (TEX_SIZE - 1)

typedef enum {
    TEX_GRANITE,
    TEX_PILLAR,
    TEX_SANCTUM,
    TEX_DOOR,
    TEX_FLOOR,
    TEX_CEILING,
    TEX_WATER,
    /* sprites: pixels with alpha 0 are transparent. Every enemy's
     * awake frame must sit at its dormant frame + 1 — entity.c wakes
     * a statue by adding 1 to its texture id. */
    TEX_GUARDIAN,
    TEX_GUARDIAN_AWAKE,
    TEX_YALI,
    TEX_YALI_AWAKE,
    TEX_NAGA,
    TEX_NAGA_AWAKE,
    TEX_BOSS,       /* the Asura King, dormant on his throne floor */
    TEX_BOSS_AWAKE,
    TEX_ARCHER,     /* asura bowman of the war-band */
    TEX_ARCHER_AWAKE,
    TEX_KNIGHT,     /* armoured asura champion */
    TEX_KNIGHT_AWAKE,
    TEX_VENOM,      /* the king's green fire */
    TEX_LAMP,
    TEX_FLAMEBALL,
    TEX_ARROW,      /* a loosed arrow in flight */
    TEX_VEL,        /* first-person spear overlay */
    TEX_PLAYER,     /* the keeper, seen in third person */

    /* items: on the ground, in the hotbar, in chests */
    TEX_SWORD,
    TEX_BOW,
    TEX_SHIELD,
    TEX_POT_HEAL,
    TEX_POT_STR,
    TEX_POT_SPD,
    TEX_CHEST,
    TEX_CHEST_OPEN,

    /* puzzle pieces */
    TEX_ROTOR_N, /* rotatable pillar block; the four orientations */
    TEX_ROTOR_E,
    TEX_ROTOR_S,
    TEX_ROTOR_W,
    TEX_PLAMP_1, /* unlit puzzle lamps carved with 1-3 order pips */
    TEX_PLAMP_2,
    TEX_PLAMP_3,
    TEX_LEVER_OFF,
    TEX_LEVER_ON,
    TEX_SHRINE_OFF,
    TEX_SHRINE_ON,
    TEX_COUNT
} TextureId;

/* Generates every texture procedurally (no files needed).
 * Returns 0 on success, -1 on allocation failure. */
int textures_init(void);
void textures_shutdown(void);

/* Row-major TEX_SIZE*TEX_SIZE pixel data. Valid between init/shutdown. */
const uint32_t *texture_pixels(TextureId id);

#endif
