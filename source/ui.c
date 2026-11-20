// ui.c -- see ui.h.
//
// Visual language: a dark slate background, four accent colours (one per
// slider), and a spiral "shell" on the top screen that spins with the
// output level, with the live waveform drawn across it.

#include "ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "patch_names.h"

#define PI_F 3.14159265f

static C2D_TextBuf s_text;

static u32 c_bg, c_panel, c_track, c_dim, c_text, c_warn, c_bad;
static u32 c_accent[PARAM_COUNT];

static const char *const kButtonHints[MUSIC_KEYS] = {"<", "v", ">", "^", "Y", "B", "A", "X"};

void ui_init(void) {
    s_text = C2D_TextBufNew(4096);
    c_bg = C2D_Color32(0x12, 0x14, 0x1B, 0xFF);
    c_panel = C2D_Color32(0x1C, 0x20, 0x2A, 0xFF);
    c_track = C2D_Color32(0x2A, 0x30, 0x3D, 0xFF);
    c_dim = C2D_Color32(0x7A, 0x84, 0x99, 0xFF);
    c_text = C2D_Color32(0xEE, 0xF1, 0xF6, 0xFF);
    c_warn = C2D_Color32(0xF5, 0xB8, 0x3D, 0xFF);
    c_bad = C2D_Color32(0xF2, 0x4B, 0x4B, 0xFF);
    c_accent[PARAM_TONE] = C2D_Color32(0x3E, 0xC8, 0xE6, 0xFF);    // cyan
    c_accent[PARAM_ECHO] = C2D_Color32(0xF2, 0x55, 0x7A, 0xFF);    // pink
    c_accent[PARAM_REVERB] = C2D_Color32(0xF5, 0xB8, 0x3D, 0xFF);  // amber
    c_accent[PARAM_VOLUME] = C2D_Color32(0x6F, 0xE0, 0x8A, 0xFF);  // mint
}

void ui_exit(void) { C2D_TextBufDelete(s_text); }

void ui_begin_frame(void) { C2D_TextBufClear(s_text); }

// flags: 0, C2D_AlignCenter or C2D_AlignRight (x is the anchor).
static void text(float x, float y, float scale, u32 color, u32 flags, const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    C2D_Text t;
    C2D_TextParse(&t, s_text, buf);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, scale, scale, color);
}

static u32 key_colour(const AppState *app, int key) {
    if (app->mode == PLAY_DRUM) return c_accent[PARAM_ECHO];
    return key < 4 ? c_accent[PARAM_TONE] : c_accent[PARAM_REVERB];
}

static u32 load_colour(float load) {
    if (load >= 0.9f) return c_bad;
    if (load >= 0.7f) return c_warn;
    return c_dim;
}

// ---------------------------------------------------------------- top ----

static void draw_shell(const AppState *app, const AudioStats *stats) {
    // A logarithmic spiral of dots. It always turns slowly and speeds up
    // with the output level; each dot takes the colour of a slider.
    static float angle = 0.0f;
    angle += 0.004f + stats->level * 0.08f;
    const float cx = TOP_W / 2.0f, cy = 104.0f;
    const int dots = 56;
    for (int i = 0; i < dots; i++) {
        float t = (float)i / dots;
        float theta = angle + t * 5.5f * PI_F;
        float r = 6.0f + 78.0f * (expf(t * 1.6f) - 1.0f) / (expf(1.6f) - 1.0f);
        float size = 1.2f + 4.0f * t * (0.6f + stats->level);
        u32 col = c_accent[i % PARAM_COUNT];
        C2D_DrawCircleSolid(cx + cosf(theta) * r, cy + sinf(theta) * r * 0.8f, 0.4f, size, col);
    }
    (void)app;
}

static void draw_scope(const int16_t *scope) {
    const float y0 = 104.0f, amp = 70.0f;
    const float dx = (float)TOP_W / (SCOPE_POINTS - 1);
    u32 col = C2D_Color32(0xFF, 0xFF, 0xFF, 0xD0);
    for (int i = 0; i < SCOPE_POINTS - 1; i++) {
        float ya = y0 - scope[i] / 32768.0f * amp;
        float yb = y0 - scope[i + 1] / 32768.0f * amp;
        C2D_DrawLine(i * dx, ya, col, (i + 1) * dx, yb, col, 1.5f, 0.6f);
    }
}

static void draw_pebbles(const AppState *app) {
    const float y = 186.0f, step = TOP_W / (float)(MUSIC_KEYS + 1);
    for (int k = 0; k < MUSIC_KEYS; k++) {
        float x = step * (k + 1);
        if (app->key_lit[k]) {
            C2D_DrawCircleSolid(x, y, 0.5f, 13.0f, key_colour(app, k));
        } else {
            C2D_DrawCircleSolid(x, y, 0.5f, 8.0f, c_track);
            C2D_DrawCircleSolid(x, y, 0.5f, 5.0f, c_bg);
        }
    }
}

void ui_draw_top(const AppState *app, const AudioStats *stats, const int16_t *scope) {
    draw_shell(app, stats);
    draw_scope(scope);
    draw_pebbles(app);

    // Header: name, mode, DSP load.
    text(8, 4, 0.5f, c_dim, 0, "CLAMSHELL");
    static const char *const mode_names[] = {"NOTE", "CHORD", "DRUMS"};
    text(TOP_W / 2.0f, 4, 0.5f, c_text, C2D_AlignCenter, "%s   OCT %+d", mode_names[app->mode], app->octave);
    text(TOP_W - 8, 4, 0.5f, load_colour(stats->load_peak), C2D_AlignRight, "LOAD %2d%%  PK %2d%%",
         (int)(stats->load * 100.0f + 0.5f), (int)(stats->load_peak * 100.0f + 0.5f));

    // Patch line.
    const char *bank = app->patch < 128 ? "JUNO" : "DX7";
    text(8, 204, 0.62f, c_text, 0, "%s %03d  %s", bank, app->patch, patch_names[app->patch - PATCH_FIRST]);
    text(8, 224, 0.42f, c_dim, 0, "L chord  R drums  PAD patch/oct  SELECT bank  START quit");
    if (stats->late) text(TOP_W - 8, 224, 0.42f, c_warn, C2D_AlignRight, "gaps %lu", (unsigned long)stats->late);

    if (!app->audio_ok) {
        C2D_DrawRectSolid(40, 80, 0.9f, TOP_W - 80, 52, c_panel);
        text(TOP_W / 2.0f, 86, 0.55f, c_bad, C2D_AlignCenter, "NO AUDIO");
        text(TOP_W / 2.0f, 106, 0.45f, c_text, C2D_AlignCenter, "Copy dspfirm.cdc to sdmc:/3ds/");
    }
}

// ------------------------------------------------------------- bottom ----

static float slider_x(int i) { return SLIDER_X0 + i * (SLIDER_W + SLIDER_GAP); }

void ui_draw_bottom(const AppState *app) {
    for (int i = 0; i < PARAM_COUNT; i++) {
        float x = slider_x(i);
        float v = app->params[i];
        float fill = v * SLIDER_H;
        bool active = app->active_slider == i;
        text(x + SLIDER_W / 2.0f, 10, 0.5f, active ? c_text : c_dim, C2D_AlignCenter, "%s",
             engine_param_name((EngineParam)i));
        C2D_DrawRectSolid(x, SLIDER_Y0, 0.5f, SLIDER_W, SLIDER_H, c_track);
        C2D_DrawRectSolid(x, SLIDER_Y0 + SLIDER_H - fill, 0.5f, SLIDER_W, fill, c_accent[i]);
        C2D_DrawRectSolid(x, SLIDER_Y0 + SLIDER_H - fill - 2, 0.5f, SLIDER_W, 3, c_text);
        text(x + SLIDER_W / 2.0f, SLIDER_Y0 + SLIDER_H + 4, 0.45f, c_dim, C2D_AlignCenter, "%d",
             (int)(v * 100.0f + 0.5f));
    }

    for (int k = 0; k < MUSIC_KEYS; k++) {
        float x = (float)(k * KEY_W);
        u32 fill = app->key_lit[k] ? key_colour(app, k) : c_panel;
        u32 ink = app->key_lit[k] ? c_bg : c_text;
        C2D_DrawRectSolid(x + 1, KEYS_Y0, 0.5f, KEY_W - 2, KEYS_H, fill);
        text(x + KEY_W / 2.0f, KEYS_Y0 + 14, app->mode == PLAY_DRUM ? 0.38f : 0.55f, ink, C2D_AlignCenter, "%s",
             music_key_label(k, app->mode));
        text(x + KEY_W / 2.0f, KEYS_Y0 + 44, 0.42f, app->key_lit[k] ? ink : c_dim, C2D_AlignCenter, "%s",
             kButtonHints[k]);
    }
}

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
