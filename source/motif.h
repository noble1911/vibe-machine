// motif.h -- a single Memphis motif (triangle, squiggle, bolt...) that can
// drift, spin and be drawn with a hard drop shadow.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"
#include "theme.h"

typedef enum {
    MO_TRI,
    MO_TRI_OUTLINE,
    MO_TRI_STRIPED,
    MO_TRI_DOTTED,
    MO_CIRCLE,
    MO_RING,
    MO_BAR,
    MO_SQUIGGLE,
    MO_ZIGZAG,
    MO_BOLT,
    MO_DOTS,
    MO_CROSS,
    MO_SPOT,
    MO_COUNT
} MotifKind;

#define MO_BIT(k) (1u << (k))
#define MO_ALL (MO_BIT(MO_COUNT) - 1u)
#define MO_PUNCHY                                                                                               \
    (MO_BIT(MO_TRI) | MO_BIT(MO_TRI_OUTLINE) | MO_BIT(MO_TRI_STRIPED) | MO_BIT(MO_TRI_DOTTED) |                \
     MO_BIT(MO_CIRCLE) | MO_BIT(MO_RING) | MO_BIT(MO_BAR) | MO_BIT(MO_SQUIGGLE) | MO_BIT(MO_ZIGZAG) |          \
     MO_BIT(MO_BOLT))

typedef struct {
    float x, y, vx, vy;
    float angle, spin;
    float size;
    uint8_t kind;
    uint32_t rgb;
    uint32_t seed;
    int life, max_life;  // 0 = immortal (background motifs)
} Motif;

// A random motif of one of the kinds in `kinds` (a MO_BIT mask), coloured
// from the theme palette.
Motif motif_random(Rng *rng, const Theme *theme, float x, float y, float size_lo, float size_hi, uint32_t kinds);

// Draw at an extra horizontal offset dx (for 3D) and scale. alpha fades
// the whole motif; shadow is the hard shadow offset in pixels (0 = none).
void motif_draw(const Motif *m, float dx, float scale, uint8_t alpha, uint32_t shadow_rgb, float shadow);

// Advance by one frame. Background motifs wrap around the given box.
void motif_step(Motif *m, float wrap_w, float wrap_h);
