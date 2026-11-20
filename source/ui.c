// ui.c -- shared UI state: theme, colour helpers, text and hit tests.

#include "ui.h"

#include <stdarg.h>
#include <stdio.h>

#include "sounds.h"
#include "ui_style.h"

static C2D_TextBuf s_text;
static int s_theme;
static float s_bump;

float ui_bump(void) { return s_bump; }
void ui_bump_kick(void) { s_bump = 1.0f; }

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
    s_bump *= 0.86f;
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
    if (y < SLIDER_LABEL_Y - 2 || y > SLIDER_Y0 + SLIDER_H + 16) return -1;
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
    return x >= THEME_X - 4 && x < THEME_X + THEME_W + 4 && y >= THEME_Y - 4 && y < THEME_Y + THEME_H + 30;
}

static bool in_box(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

int ui_hit_tab(int x, int y) {
    if (y > TAB_Y + TAB_H + 3) return -1;
    for (int i = 0; i < PAGE_COUNT; i++)
        if (in_box(x, y, TAB_X0 + i * TAB_PITCH - 1, 0, TAB_W + 2, TAB_Y + TAB_H + 3)) return i;
    return -1;
}

SeqButton ui_hit_seq_button(int x, int y) {
    if (y > TAB_Y + TAB_H + 3) return SEQ_BTN_NONE;
    if (in_box(x, y, SEQ_PLAY_X - 2, 0, SEQ_BTN_W + 3, 24)) return SEQ_BTN_PLAY;
    if (in_box(x, y, SEQ_REC_X - 2, 0, SEQ_BTN_W + 3, 24)) return SEQ_BTN_REC;
    if (in_box(x, y, SEQ_BPM_DOWN_X - 2, 0, SEQ_BPM_BTN_W + 3, 24)) return SEQ_BTN_BPM_DOWN;
    if (in_box(x, y, SEQ_BPM_UP_X - 1, 0, SEQ_BPM_BTN_W + 4, 24)) return SEQ_BTN_BPM_UP;
    if (in_box(x, y, SEQ_CLEAR_X - 2, 0, SEQ_CLEAR_W + 4, 24)) return SEQ_BTN_CLEAR;
    return SEQ_BTN_NONE;
}

bool ui_hit_seq_cell(int x, int y, int *step, int *row) {
    if (x < SEQ_GRID_X || x >= SEQ_GRID_X + SEQ_STEPS * SEQ_CELL_W) return false;
    int s = (x - SEQ_GRID_X) / SEQ_CELL_W;
    if (y >= SEQ_SYNTH_Y - 2 && y < SEQ_SYNTH_Y + SEQ_SYNTH_H + 2) {
        *step = s;
        *row = 0;
        return true;
    }
    if (y >= SEQ_DRUM_Y && y < SEQ_DRUM_Y + SEQ_LANES * SEQ_DRUM_PITCH) {
        *step = s;
        *row = 1 + (y - SEQ_DRUM_Y) / SEQ_DRUM_PITCH;
        return true;
    }
    return false;
}

int ui_hit_sound_tile(int x, int y) {
    for (int i = 0; i < sound_count() && i < 9; i++) {
        int tx = SYN_TILE_X0 + (i % 3) * SYN_TILE_PITCH_X, ty = SYN_TILE_Y0 + (i / 3) * SYN_TILE_PITCH_Y;
        if (in_box(x, y, tx - 1, ty - 1, SYN_TILE_W + 2, SYN_TILE_H + 2)) return i;
    }
    return -1;
}

bool ui_in_synth_pad(int x, int y) { return in_box(x, y, SYN_PAD_X, SYN_PAD_Y, SYN_PAD_W, SYN_PAD_H); }

int ui_hit_bank_cell(int x, int y) {
    if (!in_box(x, y, BANK_X0, BANK_Y0, BANK_COLS * BANK_CELL_W, BANK_ROWS * BANK_CELL_H)) return -1;
    return ((y - BANK_Y0) / BANK_CELL_H) * BANK_COLS + (x - BANK_X0) / BANK_CELL_W;
}

int ui_hit_fx_sticker(int x, int y) {
    for (int i = 0; i < FX_COUNT; i++)
        if (in_box(x, y, FX_STICK_X - 2, FX_STICK_Y0 + i * FX_STICK_PITCH - 1, FX_STICK_W + 4, FX_STICK_H + 2)) return i;
    if (in_box(x, y, FX_STICK_X - 2, FX_HOLD_Y - 1, FX_STICK_W + 4, FX_HOLD_H + 4)) return FX_HIT_HOLD;
    return -1;
}

bool ui_in_fx_pad(int x, int y) { return in_box(x, y, FX_PAD_X, FX_PAD_Y, FX_PAD_W, FX_PAD_H); }

static void pad_pos(int x, int y, int px, int py, int pw, int ph, float *nx, float *ny) {
    const float m = 8.0f;  // inner margin so the creature never leaves the card
    float fx = (x - px - m) / (pw - 2 * m), fy = 1.0f - (y - py - m) / (ph - 2 * m);
    *nx = fx < 0 ? 0 : fx > 1 ? 1 : fx;
    *ny = fy < 0 ? 0 : fy > 1 ? 1 : fy;
}

void ui_synth_pad_pos(int x, int y, float *nx, float *ny) {
    pad_pos(x, y, SYN_PAD_X, SYN_PAD_Y + 12, SYN_PAD_W, SYN_PAD_H - 12, nx, ny);
}

void ui_fx_pad_pos(int x, int y, float *nx, float *ny) { pad_pos(x, y, FX_PAD_X, FX_PAD_Y, FX_PAD_W, FX_PAD_H, nx, ny); }

float ui_slider_value(int y) {
    float v = 1.0f - (float)(y - SLIDER_Y0) / SLIDER_H;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
