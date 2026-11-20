// ui_style.h -- colour helpers, text, and the current theme. Internal to
// the ui*.c / motif.c files.
#pragma once

#include <citro2d.h>
#include <stdint.h>

#include "theme.h"

// 0xRRGGBB + alpha -> citro2d colour.
static inline u32 rgba(uint32_t rgb, uint8_t alpha) {
    return C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

// Blend two 0xRRGGBB colours (t in 0..1) and return 0xRRGGBB.
uint32_t rgb_lerp(uint32_t a, uint32_t b, float t);
// Perceived brightness 0..1.
float rgb_luma(uint32_t rgb);
// Readable text colour on a face: white on dark faces, theme ink otherwise.
uint32_t ink_on(const Theme *t, uint32_t face_rgb);

// 0..1 pulse kicked by every note (live or sequenced); mascots squish on it.
float ui_bump(void);
void ui_bump_kick(void);

const Theme *ui_theme(void);
int ui_theme_index(void);

// Text. flags: 0, C2D_AlignCenter or C2D_AlignRight (x is the anchor).
void ui_text(float x, float y, float scale, u32 color, u32 flags, const char *fmt, ...);
// Text with a hard offset shadow.
void ui_text_fx(float x, float y, float scale, u32 color, u32 shadow, u32 flags, const char *fmt, ...);
