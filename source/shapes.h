// shapes.h -- 90s Memphis drawing primitives on top of citro2d triangles.
//
// Everything is built from triangles and rectangles, which citro2d draws
// reliably at any size (its ellipse primitive does not, in Azahar). Colours
// are citro2d colours (C2D_Color32 / rgba()).
#pragma once

#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>

void sh_rect(float x, float y, float w, float h, u32 c);
void sh_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c);

void sh_circle(float x, float y, float r, u32 c);
void sh_ring(float x, float y, float r, float thick, u32 c);
// Rounded bar ("pill") centred on (x, y).
void sh_bar(float x, float y, float len, float thick, float angle, u32 c);

// Equilateral triangles centred on (x, y) with circumradius r.
void sh_tri(float x, float y, float r, float angle, u32 c);
void sh_tri_grad(float x, float y, float r, float angle, u32 c0, u32 c1, u32 c2);
void sh_tri_outline(float x, float y, float r, float angle, float thick, u32 c);
void sh_tri_striped(float x, float y, float r, float angle, float spacing, float stripe, u32 c);
void sh_tri_dotted(float x, float y, float r, float angle, float spacing, float dot, u32 c);

void sh_squiggle(float x, float y, float len, float amp, float angle, float thick, float phase, u32 c);
void sh_zigzag(float x, float y, float len, float amp, float angle, float thick, int teeth, u32 c);
void sh_bolt(float x, float y, float size, float angle, u32 c);
void sh_blob(float x, float y, float r, uint32_t seed, u32 c);

void sh_grid(float x, float y, float w, float h, float step, u32 c);
void sh_scanlines(float x, float y, float w, float h, float spacing, float thick, u32 c);
void sh_speckles(float x, float y, float w, float h, int n, uint32_t seed, u32 c);

// A pair of googly eyes centred on (x, y). look_x/look_y (-1..1) aim the
// pupils; blink draws them shut.
void sh_eyes(float x, float y, float r, float look_x, float look_y, bool blink, u32 ink);

// Sticker card: ink border plus a hard offset shadow (shadow 0 = none).
void sh_card(float x, float y, float w, float h, u32 face, u32 ink, float shadow);

typedef enum { PAT_STRIPES, PAT_DOTS, PAT_ZIGZAG, PAT_CHECK } FillPattern;
// Rectangle in `base`, overlaid with a pattern in `ink`.
void sh_rect_pattern(float x, float y, float w, float h, u32 base, FillPattern pat, u32 ink);
