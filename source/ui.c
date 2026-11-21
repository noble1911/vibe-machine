// ui.c -- shared UI state: theme, colour helpers, text and hit tests.

#include "ui.h"

#include <stdarg.h>
#include <math.h>
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

int ui_hit_sound_rows(int x, int y) {
    if (y >= KEYROW_Y - 3 && y < KEYROW_Y + ROW_H + 2) {
        if (x >= ROW_X0 - 2 && x < ROW_X0 + ROOT_W + 2) return x < ROW_X0 + ROOT_W / 2 ? SND_HIT_ROOT_DOWN : SND_HIT_ROOT_UP;
        if (x >= SCALE_X - 2 && x < SCALE_X + SCALE_W + 2) return x < SCALE_X + SCALE_W / 2 ? SND_HIT_SCALE_PREV : SND_HIT_SCALE_NEXT;
        return SND_HIT_NONE;
    }
    if (y >= ARPROW_Y - 2 && y < ARPROW_Y + ROW_H + 4) {
        for (int i = 0; i < 4; i++)
            if (x >= ROW_X0 + i * ARP_PITCH - 2 && x < ROW_X0 + i * ARP_PITCH + ARP_W + 2) return SND_HIT_ARP_ON + i;
    }
    return SND_HIT_NONE;
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
    if (in_box(x, y, SEQ_CLEAR_X - 1, 0, SEQ_CLEAR_W + 2, 24)) return SEQ_BTN_CLEAR;
    if (in_box(x, y, SEQ_DICE_X - 1, 0, SEQ_DICE_W + 4, 24)) return SEQ_BTN_DICE;
    return SEQ_BTN_NONE;
}

bool ui_hit_seq_cell(int x, int y, int *step, int *row) {
    if (x < SEQ_GRID_X || x >= SEQ_GRID_X + SEQ_STEPS * SEQ_CELL_W) return false;
    int s = (x - SEQ_GRID_X) / SEQ_CELL_W;
    if (y >= SEQ_SYNTH_Y - 2 && y < SEQ_SYNTH_Y + SEQ_SYNTH_H + 1) {
        *step = s;
        *row = 0;
        return true;
    }
    if (y >= SEQ_SYNTH_Y + SEQ_SYNTH_H + 1 && y < SEQ_DRUM_Y) {
        *step = s;
        *row = SEQ_ROW_ACCENT;
        return true;
    }
    if (y >= SEQ_DRUM_Y && y < SEQ_DRUM_Y + SEQ_LANES * SEQ_DRUM_PITCH) {
        *step = s;
        *row = 1 + (y - SEQ_DRUM_Y) / SEQ_DRUM_PITCH;
        return true;
    }
    return false;
}

int ui_hit_synth_extra(int x, int y) {
    const int px = SYN_TILE_X0 + (SYN_PAGE_SLOT % SYN_TILE_COLS) * SYN_TILE_PITCH_X;
    const int py = SYN_TILE_Y0 + (SYN_PAGE_SLOT / SYN_TILE_COLS) * SYN_TILE_PITCH_Y;
    if (in_box(x, y, px - 1, py - 1, SYN_TILE_W + 2, SYN_TILE_H + 2)) return SYN_HIT_PAGE;
    if (in_box(x, y, SYN_TILE_X0 - 1, SYN_EDIT_Y - 1, SYN_EDIT_W + 1, SYN_TILE_H + 4)) return SYN_HIT_EDIT;
    if (in_box(x, y, SYN_KIT_X - 1, SYN_EDIT_Y - 1, SYN_EDIT_W + 1, SYN_TILE_H + 4)) return SYN_HIT_KIT;
    if (in_box(x, y, SYN_PRESET_X - 1, SYN_EDIT_Y - 1, SYN_PRESET_W + 2, SYN_TILE_H + 4)) return SYN_HIT_PRESETS;
    return SYN_HIT_NONE;
}

int ui_hit_kit(int x, int y) {
    if (in_box(x, y, KIT_PREV_X - 2, KIT_HEAD_Y - 3, KIT_ARROW_W + 3, KIT_HEAD_H + 5)) return KIT_HIT_PREV;
    if (in_box(x, y, KIT_NEXT_X - 1, KIT_HEAD_Y - 3, KIT_ARROW_W + 3, KIT_HEAD_H + 5)) return KIT_HIT_NEXT;
    if (in_box(x, y, KIT_NAME_X, KIT_HEAD_Y - 3, KIT_NAME_W, KIT_HEAD_H + 5)) return KIT_HIT_NAME;
    for (int l = 0; l < DRUM_LANES; l++)
        if (in_box(x, y, KIT_LANE_X0 + (l % 2) * KIT_LANE_PITCH_X - 1, KIT_LANE_Y0 + (l / 2) * KIT_LANE_PITCH_Y - 1,
                   KIT_LANE_W + 2, KIT_LANE_H + 2))
            return KIT_HIT_LANE + l;
    if (in_box(x, y, KIT_PREV_X - 2, KIT_SMP_Y - 2, KIT_ARROW_W + 3, KIT_SMP_H + 3)) return KIT_HIT_SPREV;
    if (in_box(x, y, KIT_SNEXT_X - 1, KIT_SMP_Y - 2, KIT_ARROW_W + 3, KIT_SMP_H + 3)) return KIT_HIT_SNEXT;
    if (in_box(x, y, KIT_MIC_X - 1, KIT_SMP_Y - 2, KIT_MIC_W + 3, KIT_SMP_H + 3)) return KIT_HIT_MIC;
    if (in_box(x, y, KIT_TUNE_X - 2, KIT_SLIDER_Y - 2, KIT_SLIDER_W + 4, KIT_SLIDER_H + 6)) return KIT_HIT_TUNE;
    if (in_box(x, y, KIT_LEVEL_X - 2, KIT_SLIDER_Y - 2, KIT_SLIDER_W + 4, KIT_SLIDER_H + 6)) return KIT_HIT_LEVEL;
    return KIT_HIT_NONE;
}

float ui_kit_slider(int x, int x0) {
    const float v = (float)(x - x0 - 3) / (KIT_SLIDER_W - 6);
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

int ui_hit_edit(int x, int y) {
    if (y >= ED_TAB_Y - 3 && y < ED_TAB_Y + ED_TAB_H + 2) {
        for (int i = 0; i < EDP_PAGES; i++)
            if (in_box(x, y, ED_TAB_X0 + i * ED_TAB_PITCH - 1, ED_TAB_Y - 3, ED_TAB_W + 2, ED_TAB_H + 5)) return EDH_TAB + i;
        if (in_box(x, y, ED_RESET_X - 1, ED_TAB_Y - 3, ED_RESET_W + 2, ED_TAB_H + 5)) return EDH_RESET;
        return EDH_NONE;
    }
    if (in_box(x, y, ED_BODY_X - 6, ED_BODY_Y - 4, ED_BODY_W + 12, ED_BODY_H + 8)) return EDH_BODY;
    return EDH_NONE;
}

// ENV zones across the graph: attack, decay, a sustain plateau, release.
#define ENV_ZA 0.28f
#define ENV_ZD 0.24f
#define ENV_ZS 0.14f
#define ENV_ZR 0.34f

void ui_env_handles(const float *p, float hx[3], float hy[3]) {
    const float gx = ED_ENV_X, gy = ED_ENV_Y, gw = ED_ENV_W, gh = ED_ENV_H;
    hx[0] = gx + gw * ENV_ZA * p[ED_ATTACK];
    hy[0] = gy;
    hx[1] = gx + gw * (ENV_ZA + ENV_ZD * p[ED_DECAY]);
    hy[1] = gy + (1.0f - p[ED_SUSTAIN]) * gh;
    hx[2] = gx + gw * (ENV_ZA + ENV_ZD + ENV_ZS + ENV_ZR * p[ED_RELEASE]);
    hy[2] = gy + gh;
}

static float unit(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

void ui_env_drag(int h, int x, int y, float *p) {
    const float gx = ED_ENV_X, gy = ED_ENV_Y, gw = ED_ENV_W, gh = ED_ENV_H;
    if (h == 0) {
        p[ED_ATTACK] = unit((x - gx) / (gw * ENV_ZA));
    } else if (h == 1) {
        p[ED_DECAY] = unit((x - gx - gw * ENV_ZA) / (gw * ENV_ZD));
        p[ED_SUSTAIN] = unit(1.0f - (y - gy) / gh);
    } else if (h == 2) {
        p[ED_RELEASE] = unit((x - gx - gw * (ENV_ZA + ENV_ZD + ENV_ZS)) / (gw * ENV_ZR));
    }
}

void ui_edit_pad_pos(int x, int y, float *nx, float *ny) {
    *nx = unit((float)(x - ED_BODY_X - 6) / (ED_BODY_W - 12));
    *ny = unit(1.0f - (float)(y - ED_BODY_Y - 6) / (ED_PAD_H - 12));
}

float ui_edit_slider(int x, int x0, int w) { return unit((float)(x - x0 - 3) / (w - 6)); }

int ui_sound_pages(void) { return (sound_count() + SOUNDS_PER_PAGE - 1) / SOUNDS_PER_PAGE; }

int ui_hit_sound_tile(int x, int y, int page) {
    for (int slot = 0; slot < SOUNDS_PER_PAGE; slot++) {
        const int i = page * SOUNDS_PER_PAGE + slot;
        if (i >= sound_count()) break;
        int tx = SYN_TILE_X0 + (slot % SYN_TILE_COLS) * SYN_TILE_PITCH_X;
        int ty = SYN_TILE_Y0 + (slot / SYN_TILE_COLS) * SYN_TILE_PITCH_Y;
        if (in_box(x, y, tx - 1, ty - 1, SYN_TILE_W + 2, SYN_TILE_H + 2)) return i;
    }
    return -1;
}

bool ui_hit_synth_dice(int x, int y) {
    return in_box(x, y, SYN_DICE_X - 3, SYN_DICE_Y - 3, SYN_DICE_W + 6, SYN_DICE_W + 4);
}

bool ui_hit_synth_rec(int x, int y) { return in_box(x, y, SYN_REC_X - 3, SYN_DICE_Y - 3, SYN_REC_W + 4, SYN_DICE_W + 4); }

int ui_hit_sampler(int x, int y) {
    if (in_box(x, y, SMP_CHOP_X - 2, SYN_DICE_Y - 3, SMP_CHOP_W + 3, SYN_DICE_W + 4)) return SMP_HIT_CHOP;
    if (in_box(x, y, SMP_TOKIT_X - 2, SYN_DICE_Y - 3, SMP_TOKIT_W + 3, SYN_DICE_W + 4)) return SMP_HIT_TOKIT;
    if (in_box(x, y, SMP_LIB_X - 1, SYN_DICE_Y - 3, SMP_LIB_W + 3, SYN_DICE_W + 4)) return SMP_HIT_LIB;
    if (in_box(x, y, SMP_WAVE_X - 8, SMP_WAVE_Y - 4, SMP_WAVE_W + 16, SMP_WAVE_H + SMP_GRIP_H + 6)) return SMP_HIT_WAVE;
    for (int m = 0; m < 2; m++)
        if (in_box(x, y, SMP_SLIDER_X0 + m * SMP_SLIDER_PITCH - 2, SMP_SLIDER_Y - 3, SMP_SLIDER_W + 4, SMP_SLIDER_H + 6))
            return SMP_HIT_SLIDER + m;
    return SMP_HIT_NONE;
}

float ui_sampler_pos(int x) {
    float v = (float)(x - SMP_WAVE_X) / SMP_WAVE_W;
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

float ui_sampler_slider(int x, int m) {
    float v = (float)(x - SMP_SLIDER_X0 - m * SMP_SLIDER_PITCH) / SMP_SLIDER_W;
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

int ui_hit_draw_shape(int x, int y) {
    for (int i = 0; i < 4; i++)
        if (in_box(x, y, DRAW_CHIP_X + i * DRAW_CHIP_PITCH - 1, DRAW_CHIP_Y - 3, DRAW_CHIP_W + 2, DRAW_CHIP_H + 5)) return i;
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

int ui_hit_tape(int x, int y) {
    for (int t = 0; t < TAPE_TRACKS; t++) {
        int ty = TAPE_TRK_Y0 + t * TAPE_TRK_PITCH;
        if (in_box(x, y, TAPE_TRK_X + TAPE_TRK_W - TAPE_MUTE_W, ty - 1, TAPE_MUTE_W + 3, TAPE_TRK_H + 2))
            return TAPE_HIT_MUTE + t;
        if (in_box(x, y, TAPE_TRK_X + TAPE_SRC_DX - 1, ty - 1, TAPE_SRC_W + 2, TAPE_TRK_H + 2)) return TAPE_HIT_SOURCE + t;
        if (in_box(x, y, TAPE_TRK_X - 2, ty - 1, TAPE_TRK_W - TAPE_MUTE_W + 2, TAPE_TRK_H + 2)) return TAPE_HIT_TRACK + t;
    }
    if (in_box(x, y, TAPE_TRK_X - 2, TAPE_LINK_Y - 1, TAPE_TRK_W + 4, TAPE_LINK_H + 4)) return TAPE_HIT_LINK;
    if (in_box(x, y, TAPE_WIN_X, TAPE_WIN_Y, TAPE_WIN_W, TAPE_WIN_H)) return TAPE_HIT_WINDOW;
    if (in_box(x, y, TAPE_REW_X - 2, TAPE_BTN_Y - 2, TAPE_BTN_W + 3, TAPE_BTN_H + 3)) return TAPE_HIT_REW;
    if (in_box(x, y, TAPE_PLAY_X - 1, TAPE_BTN_Y - 2, TAPE_BTN_W + 2, TAPE_BTN_H + 3)) return TAPE_HIT_PLAY;
    if (in_box(x, y, TAPE_REC_X - 1, TAPE_BTN_Y - 2, TAPE_BTN_W + 2, TAPE_BTN_H + 3)) return TAPE_HIT_REC;
    if (in_box(x, y, TAPE_SMP_X - 2, TAPE_BTN_Y - 2, TAPE_SMP_W + 4, TAPE_BTN_H + 3)) return TAPE_HIT_SAMPLE;
    if (in_box(x, y, TAPE_WIN_X, TAPE_SPEED_Y - 2, TAPE_WIN_W, TAPE_SPEED_H + 6)) return TAPE_HIT_SPEED;
    return TAPE_HIT_NONE;
}

float ui_tape_speed_at(int x) {
    float t = (float)(x - TAPE_WIN_X - 6) / (TAPE_WIN_W - 12);
    float s = (t < 0 ? 0 : t > 1 ? 1 : t) * 4.0f - 2.0f;
    for (int snap = -1; snap <= 1; snap++)  // detents at reverse, stop and normal
        if (fabsf(s - snap) < 0.12f) s = (float)snap;
    return s;
}

int ui_hit_arr(int x, int y) {
    if (y >= ARR_HEAD_Y - 2 && y < ARR_HEAD_Y + ARR_HEAD_H + 2) {
        if (in_box(x, y, ARR_END_X - 2, ARR_HEAD_Y - 2, ARR_END_W + 4, ARR_HEAD_H + 4)) return ARR_HIT_END;
        if (in_box(x, y, ARR_DEL_X - 2, ARR_HEAD_Y - 2, ARR_DEL_W + 4, ARR_HEAD_H + 4)) return ARR_HIT_DEL;
        for (int k = 0; k <= PARTS; k++)
            if (in_box(x, y, ARR_MUTE_X0 + k * ARR_MUTE_PITCH - 1, ARR_HEAD_Y - 2, ARR_MUTE_W + 2, ARR_HEAD_H + 4))
                return ARR_HIT_MUTE + k;
        return ARR_HIT_NONE;
    }
    if (x < ARR_X0 - 1 || y < ARR_Y0 - 1) return ARR_HIT_NONE;
    const int c = (x - ARR_X0 + 1) / ARR_PITCH_X, r = (y - ARR_Y0 + 1) / ARR_PITCH_Y;
    if (c >= ARR_COLS || r >= SEQ_ARR_ROWS / ARR_COLS) return ARR_HIT_NONE;
    return ARR_HIT_CELL + r * ARR_COLS + c;
}

bool ui_hit_mixdown(int x, int y) { return in_box(x, y, MIXDOWN_X - 2, 0, MIXDOWN_W + 4, TAB_Y + TAB_H + 3); }

int ui_hit_mix(int x, int y, int *strip) {
    if (x < MIX_X0 - 2) return MIX_HIT_NONE;
    const int k = (x - MIX_X0 + 2) / MIX_PITCH;
    if (k >= MIX_STRIPS) return MIX_HIT_NONE;
    const int lx = x - (MIX_X0 + k * MIX_PITCH);
    if (lx > MIX_W + 2) return MIX_HIT_NONE;
    *strip = k;
    if (y >= MIX_HEAD_Y - 3 && y < MIX_HEAD_Y + MIX_HEAD_H + 2) return MIX_HIT_HEAD;
    if (y >= MIX_BTN_Y - 3 && y < MIX_BTN_Y + MIX_BTN_H + 3) return lx < MIX_SOLO_DX - 1 ? MIX_HIT_MUTE : MIX_HIT_SOLO;
    if (y < MIX_HEAD_Y + MIX_HEAD_H + 2 || y >= MIX_BTN_Y - 3) return MIX_HIT_NONE;
    if (lx < MIX_FADER_DX + MIX_FADER_W + 3) return MIX_HIT_FADER;
    const int col = lx < (MIX_KNOB_DX0 + MIX_KNOB_DX1) / 2 ? 0 : 1;
    int row = (y - MIX_KNOB_Y0 + MIX_KNOB_PITCH_Y / 2) / MIX_KNOB_PITCH_Y;
    row = row < 0 ? 0 : row > 2 ? 2 : row;
    return MIX_HIT_KNOB + mix_knob_at(row, col);  // DRIVE CRUSH / ECHO VERB / TONE PAN
}

bool ui_hit_songs_button(int x, int y) {
    return in_box(x, y, SONGS_BTN_X - 3, SONGS_BTN_Y - 2, SONGS_BTN_W + 6, SONGS_BTN_H + 4);
}

int ui_hit_songs(int x, int y, bool confirming) {
    if (confirming)  // only the card answers; any other tap cancels
        return in_box(x, y, SONG_CONFIRM_X, SONG_CONFIRM_Y, SONG_CONFIRM_W, SONG_CONFIRM_H) ? SONG_HIT_CONFIRM : SONG_HIT_NONE;
    for (int i = 0; i < 5; i++)
        if (in_box(x, y, SONG_BTN_X - 2, SONG_BTN_Y0 + i * SONG_BTN_PITCH - 1, SONG_BTN_W + 4, SONG_BTN_H + 2)) return i;
    if (in_box(x, y, SONG_SCROLL_X - 2, SONG_LIST_Y0, SONG_SCROLL_W + 4, SONG_SCROLL_H)) return SONG_HIT_UP;
    if (in_box(x, y, SONG_SCROLL_X - 2, SONG_LIST_Y0 + SONG_SCROLL_H + 4, SONG_SCROLL_W + 4, SONG_SCROLL_H)) return SONG_HIT_DOWN;
    for (int r = 0; r < SONG_ROWS; r++)
        if (in_box(x, y, SONG_LIST_X, SONG_LIST_Y0 + r * SONG_ROW_PITCH - 1, SONG_LIST_W, SONG_ROW_H + 3)) return SONG_HIT_ROW + r;
    return SONG_HIT_NONE;
}

int ui_hit_fx_auto(int x, int y) {
    if (y < FX_AUTO_Y - 3 || y > FX_AUTO_Y + FX_AUTO_H + 3 || x < FX_AUTO_X0) return -1;
    int s = (int)((x - FX_AUTO_X0) / FX_AUTO_PITCH);
    return s < SEQ_STEPS ? s : -1;
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

int ui_hit_seq_slot(int x, int y) {
    if (y < SEQ_SLOT_Y - 2 || y > SEQ_SLOT_Y + SEQ_SLOT_H + 3) return -1;
    for (int i = 0; i < SEQ_SLOTS; i++) {
        int sx = SEQ_SLOT_X0 + i * SEQ_SLOT_PITCH;
        if (x >= sx - 1 && x < sx + SEQ_SLOT_PITCH - 1) return i;
    }
    if (x >= SEQ_SONG_X - 2 && x < SEQ_SONG_X + SEQ_SONG_W + 2) return SEQ_HIT_SONG;
    if (x >= SEQ_ROLL_X - 2 && x < SEQ_ROLL_X + SEQ_ROLL_W + 2) return SEQ_HIT_ROLL;
    if (x >= SEQ_BAR_X - 1 && x < SEQ_BAR_X + SEQ_BAR_W + 1) return SEQ_HIT_BAR;
    if (x >= SEQ_SWING_X - 2 && x < SEQ_SWING_X + SEQ_SWING_W + 4) return SEQ_HIT_SWING;
    return -1;
}

bool ui_hit_roll_toggle(int x, int y) {
    return in_box(x, y, ROLL_TOGGLE_X - 2, ROLL_TOGGLE_Y - 2, ROLL_TOGGLE_W + 3, ROLL_TOGGLE_H + 2);
}

int ui_hit_roll_part(int x, int y) {
    for (int k = 0; k < PARTS; k++)
        if (in_box(x, y, ROLL_PART_X0 + k * ROLL_PART_PITCH - 1, ROLL_TOGGLE_Y - 2, ROLL_PART_W + 2, ROLL_TOGGLE_H + 3)) return k;
    return -1;
}

int ui_hit_part_tab(int x, int y) {
    if (y > TAB_Y + TAB_H + 3) return -1;
    for (int k = 0; k < PARTS; k++)
        if (in_box(x, y, PART_TAB_X0 + k * PART_TAB_PITCH - 1, 0, PART_TAB_W + 2, TAB_Y + TAB_H + 3)) return k;
    return -1;
}

bool ui_hit_seq_part_chip(int x, int y) { return in_box(x, y, 2, SEQ_SYNTH_Y - 2, 19, SEQ_SYNTH_H + 3); }

bool ui_hit_roll_keys(int x, int y) {
    return in_box(x, y, ROLL_KEYS_X - 3, ROLL_Y0, ROLL_KEYS_W + 4, ROLL_ROWS * ROLL_ROW_H);
}

bool ui_roll_cell(int x, int y, int *step, int *row) {
    if (!in_box(x, y, ROLL_X0, ROLL_Y0, SEQ_STEPS * ROLL_CELL_W, ROLL_ROWS * ROLL_ROW_H)) return false;
    ui_roll_cell_clamped(x, y, step, row);
    return true;
}

void ui_roll_cell_clamped(int x, int y, int *step, int *row) {
    int s = (x - ROLL_X0) / ROLL_CELL_W, r = ROLL_ROWS - 1 - (y - ROLL_Y0) / ROLL_ROW_H;
    if (x < ROLL_X0) s = 0;
    if (y < ROLL_Y0) r = ROLL_ROWS - 1;
    *step = s < 0 ? 0 : s >= SEQ_STEPS ? SEQ_STEPS - 1 : s;
    *row = r < 0 ? 0 : r >= ROLL_ROWS ? ROLL_ROWS - 1 : r;
}

float ui_swing_at(int x) {
    float t = (float)(x - SEQ_SWING_X - 4) / (SEQ_SWING_W - 8);
    return (t < 0 ? 0 : t > 1 ? 1 : t) * SEQ_SWING_MAX;
}

float ui_slider_value(int y) {
    float v = 1.0f - (float)(y - SLIDER_Y0) / SLIDER_H;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
