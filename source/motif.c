// motif.c -- see motif.h.

#include "motif.h"

#include <math.h>

#include "shapes.h"
#include "ui_style.h"

Motif motif_random(Rng *rng, const Theme *theme, float x, float y, float size_lo, float size_hi, uint32_t kinds) {
    Motif m = {0};
    if (!kinds) kinds = MO_ALL;
    int kind;
    do kind = rng_int(rng, MO_COUNT);
    while (!(kinds & MO_BIT(kind)));
    m.kind = (uint8_t)kind;
    m.x = x;
    m.y = y;
    m.vx = rng_range(rng, -0.25f, 0.25f);
    m.vy = rng_range(rng, -0.25f, 0.25f);
    m.angle = rng_range(rng, 0.0f, 6.2831853f);
    m.spin = rng_range(rng, -0.02f, 0.02f);
    m.size = rng_range(rng, size_lo, size_hi);
    m.rgb = theme->pal[rng_int(rng, THEME_PALETTE)];
    m.seed = rng_u32(rng);
    return m;
}

static float maxf(float a, float b) { return a > b ? a : b; }

// Geometry only, in one colour; motif_draw calls it for shadow then face.
static void shape(const Motif *m, float x, float y, float s, u32 c) {
    const float a = m->angle;
    switch (m->kind) {
        case MO_TRI: sh_tri(x, y, s, a, c); break;
        case MO_TRI_OUTLINE: sh_tri_outline(x, y, s, a, maxf(2.0f, s * 0.18f), c); break;
        case MO_TRI_STRIPED: sh_tri_striped(x, y, s, a, maxf(3.0f, s * 0.24f), maxf(1.2f, s * 0.1f), c); break;
        case MO_TRI_DOTTED: sh_tri_dotted(x, y, s, a, maxf(3.0f, s * 0.26f), maxf(0.8f, s * 0.07f), c); break;
        case MO_CIRCLE: sh_circle(x, y, s * 0.6f, c); break;
        case MO_RING: sh_ring(x, y, s * 0.65f, maxf(2.0f, s * 0.22f), c); break;
        case MO_BAR: sh_bar(x, y, s * 1.5f, maxf(3.0f, s * 0.32f), a, c); break;
        case MO_SQUIGGLE: sh_squiggle(x, y, s * 2.2f, s * 0.35f, a, maxf(2.0f, s * 0.16f), (m->seed & 63) * 0.1f, c); break;
        case MO_ZIGZAG: sh_zigzag(x, y, s * 2.0f, s * 0.28f, a, maxf(2.0f, s * 0.15f), 5, c); break;
        case MO_BOLT: sh_bolt(x, y, s * 1.7f, a, c); break;
        case MO_DOTS: {
            float sp = s * 0.45f, r = maxf(1.0f, s * 0.11f), ca = cosf(a), sa = sinf(a);
            for (int i = -1; i <= 1; i++)
                for (int j = -1; j <= 1; j++) sh_circle(x + (i * ca - j * sa) * sp, y + (i * sa + j * ca) * sp, r, c);
            break;
        }
        case MO_CROSS:
            sh_bar(x, y, s * 1.2f, maxf(2.5f, s * 0.25f), a, c);
            sh_bar(x, y, s * 1.2f, maxf(2.5f, s * 0.25f), a + 1.5708f, c);
            break;
        case MO_SPOT: sh_blob(x, y, s * 0.8f, m->seed, c); break;
        default: break;
    }
}

void motif_draw(const Motif *m, float dx, float scale, uint8_t alpha, uint32_t shadow_rgb, float shadow) {
    float s = m->size * scale, x = m->x + dx, y = m->y;
    // Fine-patterned triangles read better without a shadow.
    bool shadowed = shadow > 0.0f && m->kind != MO_TRI_STRIPED && m->kind != MO_TRI_DOTTED;
    if (shadowed) shape(m, x + shadow, y + shadow, s, rgba(shadow_rgb, (uint8_t)(alpha * 0.85f)));
    shape(m, x, y, s, rgba(m->rgb, alpha));
    if (m->kind == MO_SPOT) {
        // Leopard rosette: a darker partial blob inside.
        Motif inner = *m;
        inner.seed ^= 0x5A5Au;
        sh_blob(x + s * 0.12f, y - s * 0.08f, s * 0.38f, inner.seed, rgba(shadow_rgb, (uint8_t)(alpha * 0.7f)));
    }
}

void motif_step(Motif *m, float wrap_w, float wrap_h) {
    m->x += m->vx;
    m->y += m->vy;
    m->angle += m->spin;
    if (m->max_life > 0) {
        m->vx *= 0.985f;
        m->vy *= 0.985f;
        if (m->life > 0) m->life--;
        return;
    }
    const float margin = 24.0f;
    if (m->x < -margin) m->x += wrap_w + 2 * margin;
    if (m->x > wrap_w + margin) m->x -= wrap_w + 2 * margin;
    if (m->y < -margin) m->y += wrap_h + 2 * margin;
    if (m->y > wrap_h + margin) m->y -= wrap_h + 2 * margin;
}
