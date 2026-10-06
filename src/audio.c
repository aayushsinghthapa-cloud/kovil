#include "audio.h"

#include <SDL.h>
#include <math.h>

#include "util.h"

#define TWO_PI 6.28318530717958647692
#define RATE 44100

/* A one-shot sound effect being played. A small fixed pool of voices
 * mixes over the ambience; each is pure synthesis driven by t. */
typedef struct {
    int active;
    SoundId id;
    double t;   /* seconds since triggered */
    double ph;  /* oscillator phase */
    float lp;   /* per-voice low-pass state (flame whoosh) */
    float pan;  /* 0 left .. 1 right */
    float gain;
} Voice;

#define NVOICES 6

/* All synth state lives here. audio_init seeds it; afterwards the SDL
 * audio callback thread owns it, except audio_play which mutates the
 * voice pool under SDL_LockAudioDevice. */
typedef struct {
    double drone_ph1, drone_ph2, drone_ph3;
    float rumble;      /* one-pole low-passed noise = deep cave rumble */
    Rng rng;

    /* a water drip: decaying sine, random pitch/pan/interval */
    double drip_ph;
    float drip_env, drip_freq, drip_pan;
    int drip_wait;

    /* a distant boom: low decaying sine with a falling pitch */
    double boom_ph;
    float boom_env;
    double boom_freq;
    int boom_wait;

    Voice voices[NVOICES];
} Synth;

static Synth synth;
static SDL_AudioDeviceID device = 0;
static float master_volume = 1.0f;

void audio_set_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    master_volume = v; /* read by the callback; a float store is
                        * atomic enough for a volume knob */
}

static float rand_unit(Rng *r) /* uniform in [0,1) */
{
    return (float)(rng_next(r) >> 8) / 16777216.0f;
}

/* One sample of a one-shot effect. Every sound is shaped by an
 * envelope over v->t; when the envelope ends the voice frees itself. */
static float voice_sample(Voice *v, Rng *rng)
{
    double t = v->t;
    float out = 0.0f;
    float white = rand_unit(rng) * 2.0f - 1.0f;

    switch (v->id) {
    case SND_ROAR: {
        /* falling low tone + a rough octave, like stone grinding
         * awake; 50ms attack so it swells rather than clicks */
        if (t > 0.8) { v->active = 0; break; }
        float env = t < 0.05 ? (float)(t / 0.05)
                             : (float)((0.8 - t) / 0.75);
        env *= env;
        v->ph += TWO_PI * (64.0 - 18.0 * t) / RATE;
        out = (sinf((float)v->ph) + 0.5f * sinf((float)v->ph * 2.03f))
              * env * 0.4f + white * env * 0.06f;
        break;
    }
    case SND_HIT_STONE: {
        /* short bright noise crack */
        if (t > 0.16) { v->active = 0; break; }
        float env = (float)((0.16 - t) / 0.16);
        env = env * env * env;
        out = white * env * 0.45f;
        break;
    }
    case SND_PLAYER_HURT: {
        /* dull body thud */
        if (t > 0.4) { v->active = 0; break; }
        float env = (float)((0.4 - t) / 0.4);
        env *= env;
        v->ph += TWO_PI * 85.0 / RATE;
        out = sinf((float)v->ph) * env * 0.5f;
        break;
    }
    case SND_VEL_SWING: {
        /* airy noise burst with a sine-shaped envelope: a whoosh */
        if (t > 0.2) { v->active = 0; break; }
        float env = sinf((float)(t / 0.2) * 3.14159f);
        out = white * env * 0.16f;
        break;
    }
    case SND_FLAME: {
        /* low-passed noise swelling and dying: fire leaving the hand */
        if (t > 0.5) { v->active = 0; break; }
        float env = sinf((float)(t / 0.5) * 3.14159f);
        v->lp += 0.18f * (white - v->lp);
        out = v->lp * env * 0.9f;
        break;
    }
    case SND_CHIME: {
        /* temple bell: a tone plus inharmonic partials, long decay */
        if (t > 1.6) { v->active = 0; break; }
        float env = (float)((1.6 - t) / 1.6);
        env *= env;
        v->ph += TWO_PI * 660.0 / RATE;
        out = (sinf((float)v->ph) + 0.5f * sinf((float)v->ph * 2.76f)
               + 0.3f * sinf((float)v->ph * 5.4f)) * env * 0.22f;
        break;
    }
    case SND_GRIND: {
        /* heavy stone dragged over stone: slow low-passed noise */
        if (t > 0.9) { v->active = 0; break; }
        float env = sinf((float)(t / 0.9) * 3.14159f);
        v->lp += 0.06f * (white - v->lp);
        out = v->lp * env * 1.4f;
        break;
    }
    case SND_STOMP: {
        /* one massive footfall: sub thud + stone crack */
        if (t > 0.35) { v->active = 0; break; }
        float env = (float)((0.35 - t) / 0.35);
        env *= env;
        v->ph += TWO_PI * 55.0 / RATE;
        v->lp += 0.3f * (white - v->lp);
        out = sinf((float)v->ph) * env * 0.6f + v->lp * env * 0.25f;
        break;
    }
    case SND_BOSS_DIE: {
        /* a long roar falling apart: pitch collapses, noise grows */
        if (t > 1.8) { v->active = 0; break; }
        float env = (float)((1.8 - t) / 1.8);
        double f = 90.0 - 40.0 * t;
        v->ph += TWO_PI * f / RATE;
        v->lp += 0.1f * (white - v->lp);
        out = (sinf((float)v->ph) + 0.6f * sinf((float)v->ph * 1.94f))
              * env * 0.5f + v->lp * (1.0f - env) * env * 1.2f;
        break;
    }
    case SND_DRINK: {
        /* three quick rising "glug" blips */
        if (t > 0.45) { v->active = 0; break; }
        double seg = fmod(t, 0.15);
        float env = (float)(1.0 - seg / 0.15);
        v->ph += TWO_PI * (220.0 + t * 400.0) / RATE;
        out = sinf((float)v->ph) * env * env * 0.25f;
        break;
    }
    case SND_PICKUP: {
        /* short bright pop, pitch hopping up — the classic pickup */
        if (t > 0.18) { v->active = 0; break; }
        float env = (float)((0.18 - t) / 0.18);
        v->ph += TWO_PI * (520.0 + t * 2400.0) / RATE;
        out = sinf((float)v->ph) * env * 0.22f;
        break;
    }
    }

    v->t += 1.0 / RATE;
    return out;
}

static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    Synth *s = &synth;

    for (int i = 0; i < frames; i++) {
        /* Drone: two low sines a fraction of a Hz apart. Their slow
         * beating (~0.6 Hz here) is what makes the sound feel like it
         * breathes; a third quiet tone an octave-and-fifth up reads as
         * distant resonance in stone. */
        float drone =
            0.055f * sinf((float)s->drone_ph1) +
            0.055f * sinf((float)s->drone_ph2) +
            0.018f * sinf((float)s->drone_ph3);
        s->drone_ph1 += TWO_PI * 52.0 / RATE;
        s->drone_ph2 += TWO_PI * 52.6 / RATE;
        s->drone_ph3 += TWO_PI * 156.9 / RATE;

        /* Rumble: white noise through a one-pole low-pass filter, so
         * only the sub-bass wash remains. */
        float white = rand_unit(&s->rng) * 2.0f - 1.0f;
        s->rumble += 0.015f * (white - s->rumble);
        float rumble = s->rumble * 1.4f;

        /* Drip: when the wait runs out, strike a short decaying sine
         * at a random watery pitch, then wait 2-8 seconds. */
        if (s->drip_wait-- <= 0) {
            s->drip_env = 1.0f;
            s->drip_freq = 1200.0f + rand_unit(&s->rng) * 1200.0f;
            s->drip_pan = 0.2f + rand_unit(&s->rng) * 0.6f;
            s->drip_ph = 0.0;
            s->drip_wait = (int)((2.0f + rand_unit(&s->rng) * 6.0f) * RATE);
        }
        s->drip_env *= 0.9992f;
        float drip = 0.10f * s->drip_env * sinf((float)s->drip_ph);
        s->drip_ph += TWO_PI * s->drip_freq / RATE;

        /* Boom: every 12-30 s, a deep strike whose pitch sags as it
         * decays — reads as something enormous moving far below. */
        if (s->boom_wait-- <= 0) {
            s->boom_env = 1.0f;
            s->boom_freq = 44.0;
            s->boom_ph = 0.0;
            s->boom_wait = (int)((12.0f + rand_unit(&s->rng) * 18.0f) * RATE);
        }
        s->boom_env *= 0.99991f;
        s->boom_freq *= 0.9999992;
        float boom = 0.28f * s->boom_env * sinf((float)s->boom_ph);
        s->boom_ph += TWO_PI * s->boom_freq / RATE;

        float fxl = 0.0f, fxr = 0.0f;
        for (int vi = 0; vi < NVOICES; vi++) {
            Voice *v = &s->voices[vi];
            if (!v->active)
                continue;
            float smp = voice_sample(v, &s->rng) * v->gain;
            /* constant-ish power pan, cheap version */
            fxl += smp * (1.2f - v->pan);
            fxr += smp * (0.2f + v->pan);
        }

        float centre = drone + rumble + boom;
        float l = centre + fxl + drip * (1.0f - s->drip_pan);
        float r = centre + fxr + drip * s->drip_pan;

        l *= master_volume;
        r *= master_volume;

        /* safety clamp so a mixing mistake can never blast the ears */
        if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;

        *out++ = l;
        *out++ = r;
    }
}

int audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "audio unavailable (%s); continuing silently\n",
                SDL_GetError());
        return -1;
    }

    rng_seed(&synth.rng, 0xA0D10u);
    synth.drip_wait = RATE * 3;
    synth.boom_wait = RATE * 6; /* first boom ~6s in, once the player
                                 * has relaxed */

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;

    /* no allowed-changes flags: SDL converts to whatever the hardware
     * wants, so the callback can assume exactly the spec above */
    device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (device == 0) {
        fprintf(stderr, "audio device failed (%s); continuing silently\n",
                SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }

    SDL_PauseAudioDevice(device, 0); /* 0 = unpause: start the callback */
    return 0;
}

void audio_play_at(SoundId id, float pan, float gain)
{
    if (device == 0)
        return;
    if (pan < 0.0f) pan = 0.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (gain > 1.0f) gain = 1.0f;
    if (gain <= 0.0f)
        return; /* out of earshot */

    /* The callback runs on SDL's audio thread; the lock guarantees it
     * isn't mid-mix while we hand it a new voice. */
    SDL_LockAudioDevice(device);
    for (int i = 0; i < NVOICES; i++) {
        if (!synth.voices[i].active) {
            synth.voices[i].active = 1;
            synth.voices[i].id = id;
            synth.voices[i].t = 0.0;
            synth.voices[i].ph = 0.0;
            synth.voices[i].lp = 0.0f;
            synth.voices[i].pan = pan;
            synth.voices[i].gain = gain;
            break;
        }
    }
    SDL_UnlockAudioDevice(device);
}

void audio_play(SoundId id)
{
    audio_play_at(id, 0.5f, 1.0f);
}

void audio_shutdown(void)
{
    if (device != 0) {
        SDL_CloseAudioDevice(device);
        device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}
