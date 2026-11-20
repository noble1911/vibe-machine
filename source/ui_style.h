// ui_style.h -- palette and drawing helpers shared by the UI files.
// Internal to ui*.c.
#pragma once

#include <citro2d.h>

#include "engine.h"

// Vaporwave palette.
#define VW_PINK 0xFF71CE
#define VW_CYAN 0x01CDFE
#define VW_MINT 0x05FFA1
#define VW_PURPLE 0xB967FF
#define VW_YELLOW 0xFFFB96
#define VW_NIGHT 0x120A2A  // deepest background
#define VW_DUSK 0x2B0F4A   // panels, floor
#define VW_PLUM 0x5A1E7A   // mid sky
#define VW_LILAC 0xD9C6F5  // light text / bevel highlight
#define VW_INK 0x0B0618    // bevel shadow

// RGB hex + alpha -> citro2d colour.
static inline u32 vw(u32 rgb, u8 alpha) {
    return C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

// Linear blend of two RGB hex colours, t in 0..1.
u32 vw_mix(u32 a, u32 b, float t, u8 alpha);

// The colour assigned to each slider / parameter.
u32 vw_param_rgb(int param);

// Text. flags: 0, C2D_AlignCenter or C2D_AlignRight (x is the anchor).
void ui_text(float x, float y, float scale, u32 color, u32 flags, const char *fmt, ...);
// Text with a two-tone offset shadow (the "chromatic" 90s look).
void ui_text_fx(float x, float y, float scale, u32 color, u32 shadow, u32 flags, const char *fmt, ...);

// Win95-style bevelled box. sunken swaps the light and dark edges.
void ui_bevel(float x, float y, float w, float h, u32 face, bool sunken);

// Triangle centred on (x, y) with "radius" r, rotated by angle (radians).
void ui_tri(float x, float y, float r, float angle, u32 c0, u32 c1, u32 c2);
// Outline-only triangle.
void ui_tri_outline(float x, float y, float r, float angle, u32 color, float thickness);
