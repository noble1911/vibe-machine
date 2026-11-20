// ui_bottom.c -- the touchscreen: a drifting Memphis pattern backdrop,
// sticker-card sliders with pattern fills, a theme button, and eight keys
// that each carry their own motif and drop onto their shadow when pressed.

#include "motif.h"
#include "shapes.h"
#include "ui.h"
#include "ui_style.h"

#define BG_MOTIFS 22

static Motif s_bg[BG_MOTIFS];
static unsigned s_frame;

static const char *const kButtonHints[MUSIC_KEYS] = {"<", "v", ">", "^", "Y", "B", "A", "X"};
static const uint8_t kKeyMotif[MUSIC_KEYS] = {MO_TRI,         MO_RING,   MO_BOLT,   MO_SQUIGGLE,
                                              MO_TRI_STRIPED, MO_CIRCLE, MO_ZIGZAG, MO_TRI_DOTTED};
static const FillPattern kSliderFill[PARAM_COUNT] = {PAT_STRIPES, PAT_DOTS, PAT_ZIGZAG, PAT_CHECK};

void ui_bottom_theme_changed(void) {
    const Theme *t = ui_theme();
    Rng rng = {0xC1A55u ^ (uint32_t)(ui_theme_index() * 104729 + 7)};
    for (int i = 0; i < BG_MOTIFS; i++) {
        bool spot = t->spots && rng_f(&rng) < 0.75f;
        s_bg[i] = motif_random(&rng, t, rng_f(&rng) * BOTTOM_W, rng_f(&rng) * BOTTOM_H, spot ? 13.0f : 6.0f,
                               spot ? 22.0f : 12.0f, spot ? MO_BIT(MO_SPOT) : (MO_ALL & ~MO_BIT(MO_SPOT)));
        s_bg[i].vx *= 0.5f;
        s_bg[i].vy *= 0.5f;
    }
}

void ui_bottom_update(const AppState *app) {
    s_frame = app->frame;
    for (int i = 0; i < BG_MOTIFS; i++) motif_step(&s_bg[i], BOTTOM_W, BOTTOM_H);
}

// Palette colours that are neither near-white nor near-black, so slider
// fills always read against the white card.
static uint32_t colourful(const Theme *t, int i) {
    uint32_t picks[THEME_PALETTE];
    int n = 0;
    for (int k = 0; k < THEME_PALETTE; k++) {
        float l = rgb_luma(t->pal[k]);
        if (l > 0.12f && l < 0.9f) picks[n++] = t->pal[k];
    }
    return n ? picks[i % n] : t->pal[i % THEME_PALETTE];
}

static float slider_x(int i) { return SLIDER_X0 + i * (SLIDER_W + SLIDER_GAP); }

static void draw_slider(const AppState *app, int i) {
    const Theme *t = ui_theme();
    const float x = slider_x(i), v = app->params[i];
    const uint32_t col = colourful(t, i);
    const u32 ink = rgba(t->ink, 0xFF);

    sh_card(x, 8, SLIDER_W, 17, rgba(col, 0xFF), ink, 2);
    ui_text(x + SLIDER_W / 2.0f, 8.5f, 0.42f, rgba(ink_on(t, col), 0xFF), C2D_AlignCenter, "%s",
            engine_param_name((EngineParam)i));

    sh_card(x, SLIDER_Y0, SLIDER_W, SLIDER_H, rgba(t->card, 0xFF), ink, 3);
    const float ix = x + 2, iw = SLIDER_W - 4, iy = SLIDER_Y0 + 2, ih = SLIDER_H - 4, fill = v * ih;
    sh_rect_pattern(ix, iy + ih - fill, iw, fill, rgba(col, 0xFF), kSliderFill[i], rgba(t->ink, 0x50));
    const float ly = iy + ih - fill;
    sh_rect(ix, ly - 1, iw, 2, ink);

    // Triangle pointer in the gutter, bigger while dragged.
    float r = app->active_slider == i ? 8.0f : 6.0f;
    sh_tri(x + SLIDER_W + 8, ly + 2, r, 3.14159f, ink);
    sh_tri(x + SLIDER_W + 6, ly, r, 3.14159f, rgba(col, 0xFF));

    sh_card(x + SLIDER_W / 2.0f - 15, SLIDER_Y0 + SLIDER_H + 5, 30, 15, rgba(t->card, 0xFF), ink, 2);
    ui_text(x + SLIDER_W / 2.0f, SLIDER_Y0 + SLIDER_H + 5, 0.4f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter,
            "%d", (int)(v * 100.0f + 0.5f));
}

static void draw_theme_button(void) {
    const Theme *t = ui_theme();
    const u32 ink = rgba(t->ink, 0xFF);
    sh_card(THEME_X, THEME_Y, THEME_W, THEME_H, rgba(t->card, 0xFF), ink, 3);
    // A tiny Memphis composition in this theme's colours.
    const float cx = THEME_X + THEME_W / 2.0f, cy = THEME_Y + 30;
    sh_tri(cx - 11, cy - 8, 11, -1.5708f + s_frame * 0.01f, rgba(t->pal[0], 0xFF));
    sh_circle(cx + 12, cy - 12, 6, rgba(t->pal[1], 0xFF));
    sh_bolt(cx + 11, cy + 9, 18, 0.35f, rgba(t->pal[2], 0xFF));
    sh_squiggle(cx - 9, cy + 13, 20, 3, 0, 2.2f, s_frame * 0.08f, rgba(t->pal[3], 0xFF));
    sh_circle(cx + 1, cy + 1, 2.5f, rgba(t->pal[4], 0xFF));
    ui_text(cx, THEME_Y + THEME_H - 17, 0.38f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "%d/%d",
            ui_theme_index() + 1, ui_theme_count());

    uint32_t face = t->pal[0];
    sh_card(THEME_X, THEME_Y + THEME_H + 7, THEME_W, 17, rgba(face, 0xFF), ink, 2);
    ui_text(cx, THEME_Y + THEME_H + 7.5f, 0.42f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter, "THEME");
}

static void draw_key(const AppState *app, int k) {
    const Theme *t = ui_theme();
    const bool lit = app->key_lit[k];
    const uint32_t face = t->pal[k % THEME_PALETTE];
    const float w = KEY_W - 5, h = KEYS_H - 3;
    const float x = k * KEY_W + 2 + (lit ? 3 : 0), y = KEYS_Y0 + (lit ? 3 : 0);

    // Pressed keys drop onto their shadow and turn into a white card.
    uint32_t key_face = lit ? t->card : face;
    sh_card(x, y, w, h, rgba(key_face, 0xFF), rgba(t->ink, 0xFF), lit ? 0 : 3);

    Motif m = {0};
    m.kind = kKeyMotif[k];
    m.x = x + w / 2;
    m.y = y + 34;
    m.size = lit ? 12.0f : 10.0f;
    m.angle = lit ? s_frame * 0.12f : -1.5708f;
    m.rgb = lit ? face : ink_on(t, face);
    m.seed = (uint32_t)k * 977u;
    motif_draw(&m, 0, 1.0f, lit ? 0xFF : 0x48, t->ink, lit ? 1.5f : 0.0f);

    uint32_t ink = ink_on(t, key_face);
    ui_text(x + w / 2, y + 5, app->mode == PLAY_DRUM ? 0.36f : 0.55f, rgba(ink, 0xFF), C2D_AlignCenter, "%s",
            music_key_label(k, app->mode));
    ui_text(x + w / 2, y + 46, 0.4f, rgba(ink, 0xC0), C2D_AlignCenter, "%s", kButtonHints[k]);
}

void ui_draw_bottom(const AppState *app) {
    const Theme *t = ui_theme();
    C2D_DrawRectangle(0, 0, 0.5f, BOTTOM_W, BOTTOM_H, rgba(t->bg_a, 0xFF), rgba(t->bg_a, 0xFF), rgba(t->bg_b, 0xFF),
                      rgba(t->bg_b, 0xFF));
    if (t->grid) sh_grid(0, 0, BOTTOM_W, BOTTOM_H, 16, rgba(t->grid_rgb, 0xFF));
    for (int i = 0; i < BG_MOTIFS; i++) motif_draw(&s_bg[i], 0, 1.0f, 0xE0, t->ink, 1.5f);

    for (int i = 0; i < PARAM_COUNT; i++) draw_slider(app, i);
    draw_theme_button();
    for (int k = 0; k < MUSIC_KEYS; k++) draw_key(app, k);
}
