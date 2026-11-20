// rng.h -- tiny deterministic random numbers for visuals (an LCG).
// Deterministic so a theme's pattern looks the same every time.
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t s;
} Rng;

static inline uint32_t rng_u32(Rng *r) {
    r->s = r->s * 1664525u + 1013904223u;
    return r->s;
}

// 0 <= x < 1
static inline float rng_f(Rng *r) { return (float)(rng_u32(r) >> 8) / 16777216.0f; }

static inline float rng_range(Rng *r, float lo, float hi) { return lo + (hi - lo) * rng_f(r); }

static inline int rng_int(Rng *r, int n) { return (int)((rng_u32(r) >> 8) % (uint32_t)n); }
