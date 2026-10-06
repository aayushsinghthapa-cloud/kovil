#ifndef KOVIL_AUDIO_H
#define KOVIL_AUDIO_H

/* Ambient temple soundscape plus one-shot effects, all synthesised in
 * code — no sound files. audio_init returns 0 on success; on failure
 * the game continues silently (audio must never stop the game). */

typedef enum {
    SND_ROAR,        /* a guardian waking */
    SND_HIT_STONE,   /* weapon striking stone flesh */
    SND_PLAYER_HURT, /* the player taking a blow */
    SND_VEL_SWING,   /* spear swing whoosh */
    SND_FLAME,       /* thrown flame whoosh */
    SND_CHIME,       /* temple bell: a puzzle solved */
    SND_GRIND,       /* stone grinding: doors, rotors, sluice */
    SND_STOMP,       /* the Asura King's heavy strike */
    SND_BOSS_DIE,    /* long falling roar as the king crumbles */
    SND_DRINK,       /* gulping a potion */
    SND_PICKUP       /* an item hopping into the pack */
} SoundId;

/* Master volume 0..1, applied to everything (set from kovil.ini). */
void audio_set_volume(float v);

int audio_init(void);
void audio_shutdown(void);

/* Fire-and-forget; safe to call even if audio failed to initialise. */
void audio_play(SoundId id);

/* Positional variant: pan 0 = hard left .. 1 = hard right, gain 0..1.
 * Callers compute both from where the sound source stands relative
 * to the player's facing. */
void audio_play_at(SoundId id, float pan, float gain);

#endif
