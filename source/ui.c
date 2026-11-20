// ui.c -- shared UI state, palette helpers, text and touch hit tests.

#include "ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "ui_style.h"

static C2D_TextBuf s_text;

void ui_top_init(void);
void ui_top_update(const AppState *app, const AudioStats *stats);
void ui_bottom_init(void);

void ui_init(void) {
    s_text = C2D_TextBufNew(4096);
    ui_top_init();
    ui_bottom_init();
}

void ui_exit(void) { C2D_TextBufDelete(s_text); }

void ui_update(const AppState *app, const AudioStats *stats) {
    C2D_TextBufClear(s_text);
    ui_top_update(app, stats);
}

u32 vw_mix(u32 a, u32 b, float t, u8 alpha) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int r = (int)(((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t);
    int g = (int)(((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t);
    int bl = (int)((a & 0xFF) * (1 - t) + (b & 0xFF) * t);
    return C2D_Color32((u8)r, (u8)g, (u8)bl, alpha);
}

u32 vw_param_rgb(int param) {
    static const u32 rgb[PARAM_COUNT] = {VW_CYAN, VW_PINK, VW_PURPLE, VW_MINT};
    return (param >= 0 && param < PARAM_COUNT) ? rgb[param] : VW_LILAC;
}

static void draw_string(float x, float y, float scale, u32 color, u32 flags, const char *s) {
    C2D_Text t;
    C2D_TextParse(&t, s_text, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, scale, scale, color);
}

void ui_text(float x, float y, float scale, u32 color, u32 flags, const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    draw_string(x, y, scale, color, flags, buf);
}

void ui_text_fx(float x, float y, float scale, u32 color, u32 shadow, u32 flags, const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    draw_string(x + 1.5f, y + 1.5f, scale, shadow, flags, buf);
    draw_string(x, y, scale, color, flags, buf);
}

void ui_bevel(float x, float y, float w, float h, u32 face, bool sunken) {
    u32 light = vw(VW_LILAC, 0xFF), dark = vw(VW_INK, 0xFF);
    if (sunken) {
        u32 t = light;
        light = dark;
        dark = t;
    }
    C2D_DrawRectSolid(x, y, 0.5f, w, h, dark);           // bottom/right edge
    C2D_DrawRectSolid(x, y, 0.5f, w - 2, h - 2, light);  // top/left edge
    C2D_DrawRectSolid(x + 2, y + 2, 0.5f, w - 4, h - 4, face);
}

void ui_tri(float x, float y, float r, float angle, u32 c0, u32 c1, u32 c2) {
    const float k = 2.0943951f;  // 120 degrees
    C2D_DrawTriangle(x + cosf(angle) * r, y + sinf(angle) * r, c0,
                     x + cosf(angle + k) * r, y + sinf(angle + k) * r, c1,
                     x + cosf(angle + 2 * k) * r, y + sinf(angle + 2 * k) * r, c2, 0.5f);
}

void ui_tri_outline(float x, float y, float r, float angle, u32 color, float thickness) {
    const float k = 2.0943951f;
    float px[3], py[3];
    for (int i = 0; i < 3; i++) {
        px[i] = x + cosf(angle + i * k) * r;
        py[i] = y + sinf(angle + i * k) * r;
    }
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        C2D_DrawLine(px[i], py[i], color, px[j], py[j], color, thickness, 0.5f);
    }
}

// ------------------------------------------------------------ hit tests ----

static float slider_x(int i) { return SLIDER_X0 + i * (SLIDER_W + SLIDER_GAP); }

int ui_hit_slider(int x, int y) {
    // Generous vertical margin so a drag that overshoots still counts.
    if (y < SLIDER_Y0 - 24 || y > SLIDER_Y0 + SLIDER_H + 16) return -1;
    for (int i = 0; i < PARAM_COUNT; i++) {
        float sx = slider_x(i);
        if (x >= sx - SLIDER_GAP / 2 && x < sx + SLIDER_W + SLIDER_GAP / 2) return i;
    }
    return -1;
}

int ui_hit_key(int x, int y) {
    if (y < KEYS_Y0 || y >= KEYS_Y0 + KEYS_H) return -1;
    int k = x / KEY_W;
    return (k >= 0 && k < MUSIC_KEYS) ? k : -1;
}

float ui_slider_value(int y) {
    float v = 1.0f - (float)(y - SLIDER_Y0) / SLIDER_H;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
