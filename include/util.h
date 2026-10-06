#ifndef KOVIL_UTIL_H
#define KOVIL_UTIL_H

#include <stdint.h>

/* Hand-rolled xorshift32 PRNG (Marsaglia 2003). Three shift-xors give a
 * full-period 2^32-1 generator — tiny, fast, and unlike rand() it is
 * deterministic across platforms, which matters once map generation
 * takes a seed (same seed = same temple, on any machine). */
typedef struct {
    uint32_t state; /* must never be zero */
} Rng;

void rng_seed(Rng *r, uint32_t seed);
uint32_t rng_next(Rng *r);
/* Uniform integer in [0, n). n must be > 0. */
uint32_t rng_range(Rng *r, uint32_t n);

#endif
