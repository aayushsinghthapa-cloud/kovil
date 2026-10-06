#include "util.h"

void rng_seed(Rng *r, uint32_t seed)
{
    /* xorshift is stuck forever at zero, so remap that one seed. */
    r->state = seed ? seed : 0x9E3779B9u;
}

uint32_t rng_next(Rng *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

uint32_t rng_range(Rng *r, uint32_t n)
{
    /* Modulo bias exists but is negligible for our small n against a
     * 32-bit generator, and texture/map generation doesn't need
     * cryptographic uniformity. */
    return rng_next(r) % n;
}
