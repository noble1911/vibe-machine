// ui_bottom.c -- the touchscreen.
//
// Both pages share a drifting Memphis pattern backdrop, a tab row and the
// eight keys along the bottom (each with its own motif; pressed keys drop
// onto their shadow).
//  * SOUND: sticker-card sliders with pattern fills and the THEME button.
//  * SEQ:   transport stickers and the 16-step grid -- a synth row above
//           eight drum lanes. Cells under the playhead pop up off the card.

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
static const char *const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

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

// Palette colours that are neither near-white nor near-black, so fills
// always read against the white card.
static uint32_t colourful(const Theme *t, int i) {
    uint32_t picks[THEME_PALETTE];
    int n = 0;
    for (int k = 0; k < THEME_PALETTE; k++) {
        float l = rgb_luma(t->pal[k]);
        if (l > 0.12f && l < 0.9f) picks[n++] = t->pal[k];
    }
    return n ? picks[i % n] : t->pal[i % THEME_PALETTE];
}

// A sticker button: card + centred label, raised unless `down`.
static void sticker(float x, float y, float w, float h, uint32_t face, bool down, const char *label) {
    const Theme *t = ui_theme();
    float o = down ? 2.0f : 0.0f;
    sh_card(x + o, y + o, w, h, rgba(face, 0xFF), rgba(t->ink, 0xFF), down ? 0.0f : 2.0f);
    if (label) ui_text(x + o + w / 2, y + o + 0.5f, 0.42f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter, "%s", label);
}

// ------------------------------------------------------------- tabs ----

static void draw_tabs(const AppState *app) {
    const Theme *t = ui_theme();
    bool sound = app->page == PAGE_SOUND;
    sticker(TAB_SOUND_X, TAB_Y, TAB_SOUND_W, TAB_H, sound ? t->pal[0] : t->card, !sound, "SOUND");
    sticker(TAB_SEQ_X, TAB_Y, TAB_SEQ_W, TAB_H, !sound ? t->pal[0] : t->card, sound, "SEQ");
}

// ------------------------------------------------------- SOUND page ----

static float slider_x(int i) { return SLIDER_X0 + i * (SLIDER_W + SLIDER_GAP); }

static void draw_slider(const AppState *app, int i) {
    const Theme *t = ui_theme();
    const float x = slider_x(i), v = app->params[i];
    const uint32_t col = colourful(t, i);
    const u32 ink = rgba(t->ink, 0xFF);

    sticker(x, SLIDER_LABEL_Y, SLIDER_W, 16, col, false, engine_param_name((EngineParam)i));

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
    sh_card(THEME_X, THEME_Y, THEME_W, THEME_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 3);
    // A tiny Memphis composition in this theme's colours.
    const float cx = THEME_X + THEME_W / 2.0f, cy = THEME_Y + 26;
    sh_tri(cx - 11, cy - 8, 10, -1.5708f + s_frame * 0.01f, rgba(t->pal[0], 0xFF));
    sh_circle(cx + 12, cy - 11, 5.5f, rgba(t->pal[1], 0xFF));
    sh_bolt(cx + 11, cy + 8, 16, 0.35f, rgba(t->pal[2], 0xFF));
    sh_squiggle(cx - 9, cy + 11, 18, 3, 0, 2.2f, s_frame * 0.08f, rgba(t->pal[3], 0xFF));
    sh_circle(cx + 1, cy + 1, 2.5f, rgba(t->pal[4], 0xFF));
    ui_text(cx, THEME_Y + THEME_H - 15, 0.36f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "%d/%d",
            ui_theme_index() + 1, ui_theme_count());
    sticker(THEME_X, THEME_Y + THEME_H + 7, THEME_W, 16, t->pal[0], false, "THEME");
}

static void draw_sound_page(const AppState *app) {
    for (int i = 0; i < PARAM_COUNT; i++) draw_slider(app, i);
    draw_theme_button();
}

// --------------------------------------------------------- SEQ page ----

static void draw_transport(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqStatus *st = &app->seq;
    const u32 ink = rgba(t->ink, 0xFF);

    // Play / stop.
    uint32_t face = st->playing ? t->pal[3] : t->card;
    sticker(SEQ_PLAY_X, TAB_Y, SEQ_BTN_W, TAB_H, face, false, 0);
    float cx = SEQ_PLAY_X + SEQ_BTN_W / 2.0f, cy = TAB_Y + TAB_H / 2.0f;
    u32 icon = rgba(ink_on(t, face), 0xFF);
    if (st->playing)
        sh_rect(cx - 5, cy - 5, 10, 10, icon);
    else
        sh_tri(cx + 1, cy, 7, 0.0f, icon);

    // Record: blinks while armed.
    bool blink = st->recording && ((s_frame / 15) & 1);
    face = st->recording ? (blink ? t->pal[0] : t->card) : t->card;
    sticker(SEQ_REC_X, TAB_Y, SEQ_BTN_W, TAB_H, face, false, 0);
    cx = SEQ_REC_X + SEQ_BTN_W / 2.0f;
    sh_circle(cx, cy, 5.5f, st->recording ? rgba(blink ? ink_on(t, face) : t->pal[0], 0xFF) : ink);

    // Tempo.
    sticker(SEQ_BPM_DOWN_X, TAB_Y, SEQ_BPM_BTN_W, TAB_H, t->pal[1], false, 0);
    sh_rect(SEQ_BPM_DOWN_X + 5, cy - 1.5f, SEQ_BPM_BTN_W - 10, 3, rgba(ink_on(t, t->pal[1]), 0xFF));
    sh_card(SEQ_BPM_VALUE_X, TAB_Y, 42, TAB_H, rgba(t->card, 0xFF), ink, 2);
    ui_text(SEQ_BPM_VALUE_X + 21, TAB_Y + 0.5f, 0.42f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "%d",
            st->bpm);
    sticker(SEQ_BPM_UP_X, TAB_Y, SEQ_BPM_BTN_W, TAB_H, t->pal[1], false, 0);
    u32 plus = rgba(ink_on(t, t->pal[1]), 0xFF);
    sh_rect(SEQ_BPM_UP_X + 5, cy - 1.5f, SEQ_BPM_BTN_W - 10, 3, plus);
    sh_rect(SEQ_BPM_UP_X + SEQ_BPM_BTN_W / 2.0f - 1.5f, cy - 4, 3, 8, plus);

    sticker(SEQ_CLEAR_X, TAB_Y, SEQ_CLEAR_W, TAB_H, t->pal[4], false, "CLR");
}

static void draw_grid(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqPattern *p = &app->pattern;
    const SeqStatus *st = &app->seq;
    const u32 ink = rgba(t->ink, 0xFF);
    const float top = SEQ_SYNTH_Y - 3, bottom = SEQ_DRUM_Y + SEQ_LANES * SEQ_DRUM_PITCH + 2;

    sh_card(2, top, BOTTOM_W - 4, bottom - top, rgba(t->card, 0xFF), ink, 3);

    // Beat shading (every other group of four) and the playhead column.
    for (int s = 0; s < SEQ_STEPS; s++) {
        float x = SEQ_GRID_X + s * SEQ_CELL_W;
        if ((s / 4) & 1) sh_rect(x, top + 2, SEQ_CELL_W, bottom - top - 4, rgba(t->ink, 0x12));
        if (st->playing && s == st->step) sh_rect(x, top + 2, SEQ_CELL_W, bottom - top - 4, rgba(t->pal[0], 0x55));
    }

    // Row chips on the left: a squiggle for the synth, key motifs for drums.
    sh_card(5, SEQ_SYNTH_Y, 15, SEQ_SYNTH_H, rgba(t->pal[1], 0xFF), ink, 0);
    sh_squiggle(12.5f, SEQ_SYNTH_Y + SEQ_SYNTH_H / 2.0f, 9, 2, 0, 1.6f, 0, rgba(ink_on(t, t->pal[1]), 0xFF));
    for (int lane = 0; lane < SEQ_LANES; lane++) {
        uint32_t face = t->pal[lane % THEME_PALETTE];
        float y = SEQ_DRUM_Y + lane * SEQ_DRUM_PITCH;
        sh_card(5, y, 15, SEQ_DRUM_H, rgba(face, 0xFF), ink, 0);
        Motif m = {0};
        m.kind = kKeyMotif[lane];
        m.x = 12.5f;
        m.y = y + SEQ_DRUM_H / 2.0f;
        m.size = 4.0f;
        m.angle = -1.5708f;
        m.rgb = ink_on(t, face);
        motif_draw(&m, 0, 1.0f, 0xFF, t->ink, 0);
    }

    for (int s = 0; s < SEQ_STEPS; s++) {
        const float x = SEQ_GRID_X + s * SEQ_CELL_W;
        const bool head = st->playing && s == st->step;
        const float pop = head ? -2.0f : 0.0f;  // cells under the playhead lift off

        const SeqNote *n = &p->synth[s];
        if (n->count) {
            uint32_t face = colourful(t, 1);
            sh_card(x + 1 + pop, SEQ_SYNTH_Y + pop, SEQ_CELL_W - 2, SEQ_SYNTH_H, rgba(face, 0xFF), ink, head ? 3 : 1.5f);
            ui_text(x + SEQ_CELL_W / 2.0f + pop, SEQ_SYNTH_Y + 1.5f + pop, 0.34f, rgba(ink_on(t, face), 0xFF),
                    C2D_AlignCenter, "%s", kNoteNames[n->notes[0] % 12]);
            if (n->count > 1) sh_circle(x + SEQ_CELL_W - 4 + pop, SEQ_SYNTH_Y + 3 + pop, 1.6f, ink);
        } else {
            sh_rect(x + 2, SEQ_SYNTH_Y + 2, SEQ_CELL_W - 4, SEQ_SYNTH_H - 4, rgba(t->ink, 0x18));
        }

        for (int lane = 0; lane < SEQ_LANES; lane++) {
            float y = SEQ_DRUM_Y + lane * SEQ_DRUM_PITCH;
            if (p->drums[s] & (1u << lane)) {
                uint32_t face = t->pal[lane % THEME_PALETTE];
                sh_card(x + 1 + pop, y + pop, SEQ_CELL_W - 2, SEQ_DRUM_H, rgba(face, 0xFF), ink, head ? 3 : 1.5f);
            } else {
                sh_rect(x + SEQ_CELL_W / 2.0f - 1.5f, y + SEQ_DRUM_H / 2.0f - 1.5f, 3, 3, rgba(t->ink, 0x40));
            }
        }
    }
}

static void draw_seq_page(const AppState *app) {
    draw_transport(app);
    draw_grid(app);
}

// ------------------------------------------------------------- keys ----

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

    draw_tabs(app);
    if (app->page == PAGE_SEQ)
        draw_seq_page(app);
    else
        draw_sound_page(app);
    for (int k = 0; k < MUSIC_KEYS; k++) draw_key(app, k);
}
