// ui.c -- shared UI state: theme, colour helpers, text and hit tests.

#include "ui.h"

#include <stdarg.h>
#include <stdio.h>

#include "ui_style.h"

static C2D_TextBuf s_text;
static int s_theme;

void ui_top_init(void);
void ui_top_theme_changed(void);
void ui_top_update(const AppState *app, const AudioStats *stats);
void ui_bottom_theme_changed(void);
void ui_bottom_update(const AppState *app);

void ui_init(void) {
    s_text = C2D_TextBufNew(4096);
    ui_top_init();
    ui_set_theme(0);
}

void ui_exit(void) { C2D_TextBufDelete(s_text); }

void ui_set_theme(int index) {
    int n = theme_count();
    s_theme = ((index % n) + n) % n;
    ui_top_theme_changed();
    ui_bottom_theme_changed();
}

int ui_theme_count(void) { return theme_count(); }

const Theme *ui_theme(void) { return theme_get(s_theme); }

int ui_theme_index(void) { return s_theme; }

void ui_update(const AppState *app, const AudioStats *stats) {
    C2D_TextBufClear(s_text);
    ui_top_update(app, stats);
    ui_bottom_update(app);
}

uint32_t rgb_lerp(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        float ca = (float)((a >> shift) & 0xFF), cb = (float)((b >> shift) & 0xFF);
        out |= ((uint32_t)(ca + (cb - ca) * t) & 0xFF) << shift;
    }
    return out;
}

float rgb_luma(uint32_t rgb) {
    return (0.299f * ((rgb >> 16) & 0xFF) + 0.587f * ((rgb >> 8) & 0xFF) + 0.114f * (rgb & 0xFF)) / 255.0f;
}

uint32_t ink_on(const Theme *t, uint32_t face_rgb) {
    if (rgb_luma(face_rgb) < 0.45f) return 0xFFFFFF;
    return rgb_luma(t->ink) < 0.45f ? t->ink : 0x111111;
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

// ------------------------------------------------------------ hit tests ----

static float slider_x(int i) { return SLIDER_X0 + i * (SLIDER_W + SLIDER_GAP); }

int ui_hit_slider(int x, int y) {
    // Generous vertical margin so a drag that overshoots still counts.
    if (y < SLIDER_Y0 - 24 || y > SLIDER_Y0 + SLIDER_H + 16) return -1;
    for (int i = 0; i < PARAM_COUNT; i++) {
        float sx = slider_x(i);
        if (x >= sx - SLIDER_GAP / 2.0f && x < sx + SLIDER_W + SLIDER_GAP / 2.0f) return i;
    }
    return -1;
}

int ui_hit_key(int x, int y) {
    if (y < KEYS_Y0 || y >= KEYS_Y0 + KEYS_H) return -1;
    int k = x / KEY_W;
    return (k >= 0 && k < MUSIC_KEYS) ? k : -1;
}

bool ui_hit_theme(int x, int y) {
    return x >= THEME_X - 4 && x < THEME_X + THEME_W + 4 && y >= THEME_Y - 4 && y < THEME_Y + THEME_H + 24;
}

float ui_slider_value(int y) {
    float v = 1.0f - (float)(y - SLIDER_Y0) / SLIDER_H;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
