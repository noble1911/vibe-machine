// shapes.c -- see shapes.h.

#include "shapes.h"

#include <math.h>

#include "rng.h"

#define Z 0.5f
#define TAU 6.2831853f

void sh_rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, Z, w, h, c); }

void sh_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c) {
    C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, Z);
    C2D_DrawTriangle(x0, y0, c, x2, y2, c, x3, y3, c, Z);
}

// Rotate local point (px, py) by (ca, sa) and translate to (cx, cy).
static inline void place(float cx, float cy, float ca, float sa, float px, float py, float *ox, float *oy) {
    *ox = cx + px * ca - py * sa;
    *oy = cy + px * sa + py * ca;
}

// Always a triangle fan: citro2d's own circle/ellipse primitive does not
// render in Azahar (seen at both large and small radii), so it is never used.
void sh_circle(float x, float y, float r, u32 c) {
    if (r <= 0.0f) return;
    int segs = r < 2.5f ? 6 : r < 6.0f ? 8 : r < 12.0f ? 12 : r < 30.0f ? 20 : r < 60.0f ? 32 : 48;
    float px = x + r, py = y;
    for (int i = 1; i <= segs; i++) {
        float a = i * TAU / segs;
        float nx = x + cosf(a) * r, ny = y + sinf(a) * r;
        C2D_DrawTriangle(x, y, c, px, py, c, nx, ny, c, Z);
        px = nx;
        py = ny;
    }
}

void sh_ring(float x, float y, float r, float thick, u32 c) {
    const int segs = r < 12.0f ? 14 : 24;
    float ri = r - thick;
    if (ri < 0.0f) ri = 0.0f;
    for (int i = 0; i < segs; i++) {
        float a0 = i * TAU / segs, a1 = (i + 1) * TAU / segs;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        sh_quad(x + c0 * r, y + s0 * r, x + c1 * r, y + s1 * r, x + c1 * ri, y + s1 * ri, x + c0 * ri, y + s0 * ri,
                c);
    }
}

void sh_bar(float x, float y, float len, float thick, float angle, u32 c) {
    float ca = cosf(angle), sa = sinf(angle), hl = len / 2, ht = thick / 2;
    float ax, ay, bx, by, cx, cy, dx, dy;
    place(x, y, ca, sa, -hl, -ht, &ax, &ay);
    place(x, y, ca, sa, hl, -ht, &bx, &by);
    place(x, y, ca, sa, hl, ht, &cx, &cy);
    place(x, y, ca, sa, -hl, ht, &dx, &dy);
    sh_quad(ax, ay, bx, by, cx, cy, dx, dy, c);
    float ex, ey, fx, fy;
    place(x, y, ca, sa, -hl, 0, &ex, &ey);
    place(x, y, ca, sa, hl, 0, &fx, &fy);
    sh_circle(ex, ey, ht, c);
    sh_circle(fx, fy, ht, c);
}

static void tri_points(float x, float y, float r, float angle, float px[3], float py[3]) {
    for (int i = 0; i < 3; i++) {
        float a = angle + i * (TAU / 3.0f);
        px[i] = x + cosf(a) * r;
        py[i] = y + sinf(a) * r;
    }
}

void sh_tri(float x, float y, float r, float angle, u32 c) { sh_tri_grad(x, y, r, angle, c, c, c); }

void sh_tri_grad(float x, float y, float r, float angle, u32 c0, u32 c1, u32 c2) {
    float px[3], py[3];
    tri_points(x, y, r, angle, px, py);
    C2D_DrawTriangle(px[0], py[0], c0, px[1], py[1], c1, px[2], py[2], c2, Z);
}

void sh_tri_outline(float x, float y, float r, float angle, float thick, u32 c) {
    float px[3], py[3];
    tri_points(x, y, r, angle, px, py);
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        C2D_DrawLine(px[i], py[i], c, px[j], py[j], c, thick, Z);
        sh_circle(px[i], py[i], thick * 0.5f, c);  // fill the corner notch
    }
}

// Horizontal extent of the triangle at height y.
static int tri_span(const float px[3], const float py[3], float y, float *xl, float *xr) {
    float lo = 1e9f, hi = -1e9f;
    int hits = 0;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float y0 = py[i], y1 = py[j];
        if (y0 == y1) continue;
        if (y < fminf(y0, y1) || y > fmaxf(y0, y1)) continue;
        float x = px[i] + (y - y0) / (y1 - y0) * (px[j] - px[i]);
        lo = fminf(lo, x);
        hi = fmaxf(hi, x);
        hits++;
    }
    *xl = lo;
    *xr = hi;
    return hits >= 1;
}

void sh_tri_striped(float x, float y, float r, float angle, float spacing, float stripe, u32 c) {
    float px[3], py[3];
    tri_points(x, y, r, angle, px, py);
    float top = fminf(py[0], fminf(py[1], py[2])), bot = fmaxf(py[0], fmaxf(py[1], py[2]));
    for (float y0 = top + 0.01f; y0 < bot; y0 += spacing) {
        float y1 = fminf(y0 + stripe, bot - 0.01f);
        float l0, r0, l1, r1;
        if (y1 <= y0) break;
        if (tri_span(px, py, y0, &l0, &r0) && tri_span(px, py, y1, &l1, &r1)) sh_quad(l0, y0, r0, y0, r1, y1, l1, y1, c);
    }
}

static int inside(const float px[3], const float py[3], float x, float y) {
    float d0 = (px[1] - px[0]) * (y - py[0]) - (py[1] - py[0]) * (x - px[0]);
    float d1 = (px[2] - px[1]) * (y - py[1]) - (py[2] - py[1]) * (x - px[1]);
    float d2 = (px[0] - px[2]) * (y - py[2]) - (py[0] - py[2]) * (x - px[2]);
    int neg = (d0 < 0) || (d1 < 0) || (d2 < 0), pos = (d0 > 0) || (d1 > 0) || (d2 > 0);
    return !(neg && pos);
}

void sh_tri_dotted(float x, float y, float r, float angle, float spacing, float dot, u32 c) {
    float px[3], py[3];
    tri_points(x, y, r, angle, px, py);
    int row = 0;
    for (float gy = y - r; gy <= y + r; gy += spacing, row++) {
        float off = (row & 1) ? spacing * 0.5f : 0.0f;  // halftone offset rows
        for (float gx = x - r + off; gx <= x + r; gx += spacing)
            if (inside(px, py, gx, gy)) sh_circle(gx, gy, dot, c);
    }
}

static void polyline(const float *xs, const float *ys, int n, float thick, u32 c) {
    for (int i = 0; i + 1 < n; i++) C2D_DrawLine(xs[i], ys[i], c, xs[i + 1], ys[i + 1], c, thick, Z);
    if (thick >= 2.0f)
        for (int i = 0; i < n; i++) sh_circle(xs[i], ys[i], thick * 0.5f, c);
}

void sh_squiggle(float x, float y, float len, float amp, float angle, float thick, float phase, u32 c) {
    enum { N = 13 };
    float xs[N], ys[N], ca = cosf(angle), sa = sinf(angle);
    for (int i = 0; i < N; i++) {
        float t = (float)i / (N - 1);
        place(x, y, ca, sa, (t - 0.5f) * len, sinf(phase + t * TAU * 1.25f) * amp, &xs[i], &ys[i]);
    }
    polyline(xs, ys, N, thick, c);
}

void sh_zigzag(float x, float y, float len, float amp, float angle, float thick, int teeth, u32 c) {
    enum { MAXP = 32 };
    if (teeth > MAXP - 1) teeth = MAXP - 1;
    float xs[MAXP], ys[MAXP], ca = cosf(angle), sa = sinf(angle);
    for (int i = 0; i <= teeth; i++) {
        float t = (float)i / teeth;
        place(x, y, ca, sa, (t - 0.5f) * len, (i & 1) ? amp : -amp, &xs[i], &ys[i]);
    }
    polyline(xs, ys, teeth + 1, thick, c);
}

void sh_bolt(float x, float y, float size, float angle, u32 c) {
    // A lightning bolt as the union of a slanted parallelogram (top) and a
    // long triangle (bottom), in a unit box centred on the origin.
    static const float upper[4][2] = {{-0.12f, -0.5f}, {0.28f, -0.5f}, {0.06f, -0.02f}, {-0.30f, -0.02f}};
    static const float lower[3][2] = {{-0.12f, -0.07f}, {0.32f, -0.07f}, {-0.22f, 0.5f}};
    float ca = cosf(angle), sa = sinf(angle), qx[4], qy[4], tx[3], ty[3];
    for (int i = 0; i < 4; i++) place(x, y, ca, sa, upper[i][0] * size, upper[i][1] * size, &qx[i], &qy[i]);
    for (int i = 0; i < 3; i++) place(x, y, ca, sa, lower[i][0] * size, lower[i][1] * size, &tx[i], &ty[i]);
    sh_quad(qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], qx[3], qy[3], c);
    C2D_DrawTriangle(tx[0], ty[0], c, tx[1], ty[1], c, tx[2], ty[2], c, Z);
}

void sh_blob(float x, float y, float r, uint32_t seed, u32 c) {
    enum { N = 9 };
    Rng rng = {seed | 1u};
    float xs[N], ys[N];
    for (int i = 0; i < N; i++) {
        float a = i * TAU / N, rr = r * rng_range(&rng, 0.7f, 1.15f);
        xs[i] = x + cosf(a) * rr;
        ys[i] = y + sinf(a) * rr * 0.8f;
    }
    for (int i = 0; i < N; i++) {
        int j = (i + 1) % N;
        C2D_DrawTriangle(x, y, c, xs[i], ys[i], c, xs[j], ys[j], c, Z);
    }
}

void sh_grid(float x, float y, float w, float h, float step, u32 c) {
    for (float gx = x; gx <= x + w; gx += step) sh_rect(gx, y, 1, h, c);
    for (float gy = y; gy <= y + h; gy += step) sh_rect(x, gy, w, 1, c);
}

void sh_scanlines(float x, float y, float w, float h, float spacing, float thick, u32 c) {
    for (float gy = y; gy < y + h; gy += spacing) sh_rect(x, gy, w, thick, c);
}

void sh_speckles(float x, float y, float w, float h, int n, uint32_t seed, u32 c) {
    Rng rng = {seed | 1u};
    for (int i = 0; i < n; i++) {
        float s = rng_range(&rng, 1.0f, 2.5f);
        sh_rect(x + rng_f(&rng) * w, y + rng_f(&rng) * h, s, s * rng_range(&rng, 0.5f, 1.0f), c);
    }
}

void sh_eyes(float x, float y, float r, float look_x, float look_y, bool blink, u32 ink) {
    const u32 white = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    for (int side = -1; side <= 1; side += 2) {
        float ex = x + side * r * 1.05f;
        sh_circle(ex, y, r + 1.2f, ink);
        if (blink) {
            sh_rect(ex - r, y - 0.8f, 2 * r, 1.6f, white);
            continue;
        }
        sh_circle(ex, y, r, white);
        sh_circle(ex + look_x * r * 0.45f, y - look_y * r * 0.45f, r * 0.5f, ink);
    }
}

void sh_floppy(float x, float y, float s, u32 body, u32 label, u32 ink) {
    sh_rect(x, y, s, s, ink);
    sh_rect(x + 1, y + 1, s - 2, s - 2, body);
    sh_rect(x + s * 0.25f, y + 1, s * 0.5f, s * 0.32f, ink);    // metal shutter
    sh_rect(x + s * 0.55f, y + 2, s * 0.12f, s * 0.22f, body);  // its window
    sh_rect(x + s * 0.18f, y + s * 0.5f, s * 0.64f, s * 0.42f, label);
}

void sh_card(float x, float y, float w, float h, u32 face, u32 ink, float shadow) {
    if (shadow > 0.0f) sh_rect(x + shadow, y + shadow, w, h, ink);
    sh_rect(x, y, w, h, ink);
    sh_rect(x + 2, y + 2, w - 4, h - 4, face);
}

void sh_rect_pattern(float x, float y, float w, float h, u32 base, FillPattern pat, u32 ink) {
    if (w <= 0.0f || h <= 0.0f) return;
    sh_rect(x, y, w, h, base);
    switch (pat) {
        case PAT_STRIPES:
            for (float gy = y + h - 4; gy >= y; gy -= 6) sh_rect(x, gy, w, 2, ink);
            break;
        case PAT_DOTS: {
            int row = 0;
            for (float gy = y + h - 3; gy >= y + 1; gy -= 6, row++)
                for (float gx = x + 3 + ((row & 1) ? 3 : 0); gx < x + w - 1; gx += 6)
                    sh_circle(gx, gy, 1.4f, ink);
            break;
        }
        case PAT_ZIGZAG:
            for (float gy = y + h - 6; gy >= y + 3; gy -= 10) sh_zigzag(x + w / 2, gy, w, 2.5f, 0, 1.5f, (int)(w / 6), ink);
            break;
        case PAT_CHECK: {
            int row = 0;
            for (float gy = y + h - 6; gy >= y - 5; gy -= 6, row++)
                for (float gx = x + ((row & 1) ? 6 : 0); gx < x + w; gx += 12) {
                    float top = gy < y ? y : gy, cw = gx + 6 > x + w ? x + w - gx : 6;
                    sh_rect(gx, top, cw, gy + 6 - top, ink);
                }
            break;
        }
    }
}
