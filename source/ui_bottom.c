// ui_bottom.c -- the touchscreen.
//
// Both pages share a drifting Memphis pattern backdrop, a tab row and the
// eight keys along the bottom (each with its own motif; pressed keys drop
// onto their shadow).
//  * SOUND: sticker-card sliders with pattern fills and the THEME button.
//  * SEQ:   transport stickers and the 16-step grid -- a synth row above
//           eight drum lanes. Cells under the playhead pop up off the card.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "motif.h"
#include "tape.h"
#include "shapes.h"
#include "sounds.h"
#include "ui.h"
#include "ui_style.h"

#define BG_MOTIFS 22

static Motif s_bg[BG_MOTIFS];
// Mirrors of tape UI settings that TapeStatus doesn't carry.
static bool s_tape_armed_ui;
static float s_tape_speed_ui = 1.0f;

void ui_tape_ui_state(bool armed, float speed) {
    s_tape_armed_ui = armed;
    s_tape_speed_ui = speed;
}
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

// Recent stylus positions on the FX pad, drawn as a fading motif trail.
#define TRAIL 14
static float s_trail_x[TRAIL], s_trail_y[TRAIL];
static int s_trail_len;

void ui_bottom_update(const AppState *app) {
    s_frame = app->frame;
    if (app->page == PAGE_FX && app->pad_touch) {
        for (int i = TRAIL - 1; i > 0; i--) {
            s_trail_x[i] = s_trail_x[i - 1];
            s_trail_y[i] = s_trail_y[i - 1];
        }
        s_trail_x[0] = app->pad_x;
        s_trail_y[0] = app->pad_y;
        if (s_trail_len < TRAIL) s_trail_len++;
    } else if (s_trail_len > 0 && (s_frame & 1)) {
        s_trail_len--;  // the trail shrinks away after you lift off
    }
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

// Each part has a colour, used wherever it appears.
static uint32_t part_rgb(const Theme *t, int part) { return t->pal[(part * 2 + 1) % THEME_PALETTE]; }

// A sticker button: card + centred label, raised unless `down`.
static void sticker(float x, float y, float w, float h, uint32_t face, bool down, const char *label) {
    const Theme *t = ui_theme();
    float o = down ? 2.0f : 0.0f;
    sh_card(x + o, y + o, w, h, rgba(face, 0xFF), rgba(t->ink, 0xFF), down ? 0.0f : 2.0f);
    if (label) ui_text(x + o + w / 2, y + o + 0.5f, 0.42f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter, "%s", label);
}

// ------------------------------------------------------------- tabs ----

// Icon tabs: sliders, step grid, waveform, lightning.
static void draw_tabs(const AppState *app) {
    const Theme *t = ui_theme();
    for (int i = 0; i < PAGE_COUNT; i++) {
        const bool on = app->page == (Page)i;
        const float x = TAB_X0 + i * TAB_PITCH;
        const uint32_t face = on ? t->pal[i % THEME_PALETTE] : t->card;
        sticker(x, TAB_Y, TAB_W, TAB_H, face, !on, 0);
        const float o = on ? 0.0f : 2.0f, cx = x + TAB_W / 2.0f + o, cy = TAB_Y + TAB_H / 2.0f + o;
        const u32 ink = rgba(ink_on(t, face), 0xFF);
        switch (i) {
            case PAGE_SOUND:
                for (int b = 0; b < 3; b++) {
                    float h = 5.0f + ((b * 5 + 3) % 7);
                    sh_rect(cx - 7 + b * 5.5f, cy + 6 - h, 3, h, ink);
                }
                break;
            case PAGE_SEQ:
                for (int b = 0; b < 4; b++) sh_rect(cx - 6 + (b & 1) * 7, cy - 6 + (b >> 1) * 7, 5, 5, ink);
                break;
            case PAGE_SYNTH: sh_squiggle(cx, cy, 14, 3.5f, 0, 2.0f, s_frame * 0.1f, ink); break;
            case PAGE_MIX:  // three faders at different heights
                for (int b = 0; b < 3; b++) {
                    const float lx = cx - 5 + b * 5, ky = cy - 4 + ((b * 7 + 2) % 9);
                    sh_rect(lx - 0.75f, cy - 6, 1.5f, 12, ink);
                    sh_rect(lx - 2, ky - 1.5f, 4, 3, ink);
                }
                break;
            case PAGE_FX: sh_bolt(cx, cy, 15, 0.25f, ink); break;
            default:  // TAPE: a little cassette
                sh_rect(cx - 8, cy - 5, 16, 10, ink);
                sh_circle(cx - 3.5f, cy - 0.5f, 2.2f, rgba(face, 0xFF));
                sh_circle(cx + 3.5f, cy - 0.5f, 2.2f, rgba(face, 0xFF));
                break;
        }
    }
}

// --------------------------------------------------------- MIX page ----

#define PI_F 3.14159265f

// A knob's value as an angle (screen coordinates, y down): 0 at the bottom
// left, round over the top, 1 at the bottom right.
static float knob_angle(float v) { return PI_F * 0.75f + v * PI_F * 1.5f; }

// A thick arc between radii r0 and r1, from angle a0 to a1.
static void arc(float cx, float cy, float r0, float r1, float a0, float a1, u32 c) {
    if (a1 < a0) {
        float s = a0;
        a0 = a1;
        a1 = s;
    }
    const int n = (int)((a1 - a0) / 0.2f) + 1;
    for (int i = 0; i < n; i++) {
        const float b0 = a0 + (a1 - a0) * i / n, b1 = a0 + (a1 - a0) * (i + 1) / n;
        const float c0 = cosf(b0), s0 = sinf(b0), c1 = cosf(b1), s1 = sinf(b1);
        sh_quad(cx + c0 * r0, cy + s0 * r0, cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1, cx + c1 * r0,
                cy + s1 * r0, c);
    }
}

static int mix_sound(const AppState *app, int k) { return k == app->part ? app->sound : app->parts[k].sound; }

// The colour a strip goes by: the part's colour, and the next one for the drums.
static uint32_t strip_rgb(const Theme *t, int k) { return part_rgb(t, k); }

static bool strip_audible(const AppState *app, int k) {
    if ((app->seq.muted >> k) & 1u) return false;
    return app->mix_solo == 0 || ((app->mix_solo >> k) & 1u);
}

static const char *const kKnobShort[MIX_KNOBS] = {"DRV", "CRSH", "TONE", "PAN"};

static void draw_knob(const AppState *app, int k, int i, float cx, float cy, uint32_t col) {
    const Theme *t = ui_theme();
    const float v = app->mix[k].knob[i];
    const bool grabbed = app->mix_strip == k && app->mix_grab == MIX_GRAB_KNOB + i;
    const float r = MIX_KNOB_R + (grabbed ? 1.5f : 0.0f);
    const bool centred = i == MIX_TONE || i == MIX_PAN;
    arc(cx, cy, r + 1.0f, r + 3.0f, knob_angle(0.0f), knob_angle(1.0f), rgba(t->ink, 0x28));  // the track
    if (centred || v > 0.01f) arc(cx, cy, r + 1.0f, r + 3.0f, knob_angle(centred ? 0.5f : 0.0f), knob_angle(v), rgba(col, 0xFF));
    sh_circle(cx + 1.5f, cy + 1.5f, r, rgba(t->ink, 0xFF));  // hard shadow
    sh_circle(cx, cy, r, rgba(t->card, 0xFF));
    sh_ring(cx, cy, r, 1.5f, rgba(t->ink, 0xFF));
    const float a = knob_angle(v);
    sh_bar(cx + cosf(a) * r * 0.45f, cy + sinf(a) * r * 0.45f, r * 0.9f, 2.5f, a, rgba(t->ink, 0xFF));
    char text[16];
    if (!grabbed)
        snprintf(text, sizeof(text), "%s", kKnobShort[i]);
    else if (i == MIX_TONE)
        snprintf(text, sizeof(text), "%+d", (int)lroundf((v - 0.5f) * 100.0f));
    else if (i == MIX_PAN) {
        const int p = (int)lroundf((v - 0.5f) * 100.0f);
        if (p == 0)
            snprintf(text, sizeof(text), "C");
        else
            snprintf(text, sizeof(text), "%c%d", p < 0 ? 'L' : 'R', p < 0 ? -p : p);
    } else
        snprintf(text, sizeof(text), "%d", (int)lroundf(v * 100.0f));
    ui_text(cx, cy + r + 1.5f, grabbed ? 0.36f : 0.3f, rgba(ink_on(t, t->bg_b), 0xFF), C2D_AlignCenter, "%s", text);
}

// The fader: an LED meter in the slot, a unity mark, and the cap.
static void draw_fader(const AppState *app, int k, float x, uint32_t col) {
    const Theme *t = ui_theme();
    const u32 ink = rgba(t->ink, 0xFF);
    const float fx = x + MIX_FADER_DX, w = MIX_FADER_W, y0 = MIX_FADER_Y, h = MIX_FADER_H;
    sh_card(fx, y0, w, h, rgba(t->card, 0xFF), ink, 2);
    const float top = y0 + 5, bottom = y0 + h - 5, span = bottom - top;
    const int segs = 13;
    const float seg = span / segs;
    const int lit = (int)(app->mix_meter[k] * segs + 0.5f);
    for (int s = 0; s < segs; s++) {
        const uint32_t c = s >= segs - 2 ? t->pal[0] : col;  // the top two run hot
        sh_rect(fx + w / 2.0f - 4, bottom - (s + 1) * seg + 1, 8, seg - 1.5f, rgba(s < lit ? c : t->ink, s < lit ? 0xFF : 0x14));
    }
    const float uy = bottom - MIX_LEVEL_UNITY * span;  // 0 dB
    sh_rect(fx + 2, uy - 0.75f, 4, 1.5f, ink);
    sh_rect(fx + w - 6, uy - 0.75f, 4, 1.5f, ink);
    const bool grabbed = app->mix_strip == k && app->mix_grab == MIX_GRAB_FADER;
    const float cy = bottom - app->mix[k].level * span, ch = grabbed ? 12.0f : 10.0f;
    sh_card(fx - 2, cy - ch / 2, w + 4, ch, rgba(col, 0xFF), ink, grabbed ? 0.0f : 2.0f);
    sh_rect(fx + 1, cy - 0.75f, w - 2, 1.5f, rgba(ink_on(t, col), 0xFF));
}

static void draw_mix_page(const AppState *app) {
    const Theme *t = ui_theme();
    for (int k = 0; k < MIX_STRIPS; k++) {
        const float x = MIX_X0 + k * MIX_PITCH;
        const uint32_t col = strip_rgb(t, k);
        // Name sticker: the part and its sound (tap to play that part).
        sticker(x, MIX_HEAD_Y, MIX_W, MIX_HEAD_H, col, false, 0);
        const u32 on_col = rgba(ink_on(t, col), 0xFF);
        bool on_tape = false;
        for (int tr = 0; tr < TAPE_TRACKS; tr++) on_tape |= app->tape.used[tr] && app->tape.source[tr] == k;
        const float tx = x + MIX_W / 2.0f - (on_tape ? 6 : 0);  // make room for the cassette
        if (k < PARTS) {
            const bool cur = k == app->part;
            if (cur) sh_tri(x + 6, MIX_HEAD_Y + MIX_HEAD_H / 2.0f, 4, 0, on_col);  // the part you're playing
            ui_text(tx + (cur ? 3 : 0), MIX_HEAD_Y + 1.5f, 0.36f, on_col, C2D_AlignCenter, "%d %s", k + 1,
                    sound_get(mix_sound(app, k))->name);
        } else {
            ui_text(tx, MIX_HEAD_Y + 1.5f, 0.36f, on_col, C2D_AlignCenter, "DRUMS");
        }
        if (on_tape) {  // this strip is on tape: a little cassette
            const float qx = x + MIX_W - 9, qy = MIX_HEAD_Y + MIX_HEAD_H / 2.0f;
            sh_rect(qx - 6, qy - 4, 12, 8, on_col);
            sh_rect(qx - 4, qy - 2, 2, 2, rgba(col, 0xFF));
            sh_rect(qx + 2, qy - 2, 2, 2, rgba(col, 0xFF));
        }
        draw_fader(app, k, x, col);
        for (int i = 0; i < MIX_KNOBS; i++)
            draw_knob(app, k, i, x + ((i & 1) ? MIX_KNOB_DX1 : MIX_KNOB_DX0), (i < 2) ? MIX_KNOB_Y0 : MIX_KNOB_Y1, col);
        if (!strip_audible(app, k))  // silenced (muted, or someone else soloed): fade it back
            sh_rect(x - 1, MIX_HEAD_Y + MIX_HEAD_H + 3, MIX_W + 2, MIX_BTN_Y - MIX_HEAD_Y - MIX_HEAD_H - 6,
                    rgba(t->bg_a, 0x90));
        const bool muted = (app->seq.muted >> k) & 1u, solo = (app->mix_solo >> k) & 1u;
        sticker(x + MIX_MUTE_DX, MIX_BTN_Y, MIX_BTN_W, MIX_BTN_H, muted ? t->pal[0] : t->card, muted, "MUTE");
        sticker(x + MIX_SOLO_DX, MIX_BTN_Y, MIX_BTN_W, MIX_BTN_H, solo ? colourful(t, 3) : t->card, solo, "SOLO");
    }
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

static void draw_songs_button(void) {
    const Theme *t = ui_theme();
    sticker(SONGS_BTN_X, SONGS_BTN_Y, SONGS_BTN_W, SONGS_BTN_H, t->pal[2], false, 0);
    sh_floppy(SONGS_BTN_X + 4, SONGS_BTN_Y + 4, 14, rgba(t->pal[4], 0xFF), rgba(t->card, 0xFF), rgba(t->ink, 0xFF));
    ui_text(SONGS_BTN_X + 34, SONGS_BTN_Y + 3, 0.36f, rgba(ink_on(t, t->pal[2]), 0xFF), C2D_AlignCenter, "SONGS");
}

static void draw_sound_page(const AppState *app) {
    for (int i = 0; i < PARAM_COUNT; i++) draw_slider(app, i);
    draw_theme_button();
    draw_songs_button();
}

// --------------------------------------------------------- SONGS page ----

static void draw_songs_page(const AppState *app) {
    const Theme *t = ui_theme();
    const u32 ink = rgba(t->ink, 0xFF);
    if (app->song_count == 0) {
        sh_card(SONG_LIST_X, SONG_LIST_Y0, SONG_LIST_W, SONG_ROWS * SONG_ROW_PITCH - 4, rgba(t->card, 0xFF), ink, 3);
        sh_floppy(SONG_LIST_X + SONG_LIST_W / 2.0f - 14, SONG_LIST_Y0 + 22, 28, rgba(t->pal[1], 0xFF), rgba(t->card, 0xFF), ink);
        ui_text(SONG_LIST_X + SONG_LIST_W / 2.0f, SONG_LIST_Y0 + 60, 0.45f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "no songs yet");
        ui_text(SONG_LIST_X + SONG_LIST_W / 2.0f, SONG_LIST_Y0 + 78, 0.36f, rgba(ink_on(t, t->card), 0xA0), C2D_AlignCenter,
                "tap SAVE AS to keep this one");
    }
    for (int r = 0; r < SONG_ROWS; r++) {
        int i = app->song_scroll + r;
        if (i >= app->song_count) break;
        const SongInfo *s = &app->songs[i];
        const bool sel = i == app->song_sel, cur = strcmp(s->name, app->song_name) == 0;
        const float y = SONG_LIST_Y0 + r * SONG_ROW_PITCH;
        const uint32_t face = sel ? t->pal[i % THEME_PALETTE] : t->card;
        sticker(SONG_LIST_X, y, SONG_LIST_W, SONG_ROW_H, face, false, 0);
        sh_floppy(SONG_LIST_X + 4, y + 4, 14, rgba(t->pal[(i + 1) % THEME_PALETTE], 0xFF), rgba(t->card, 0xFF), ink);
        const uint32_t txt = ink_on(t, face);
        ui_text(SONG_LIST_X + 24, y + 3, 0.42f, rgba(txt, 0xFF), 0, "%s%s", s->name, cur ? " <" : "");
        if (s->bpm >= 0)
            ui_text(SONG_LIST_X + SONG_LIST_W - 5, y + 5, 0.32f, rgba(txt, 0xB0), C2D_AlignRight, "%d BPM  %dP", s->bpm, s->patterns);
    }
    // Scroll stickers (dimmed at either end).
    const bool up = app->song_scroll > 0, down = app->song_scroll + SONG_ROWS < app->song_count;
    sticker(SONG_SCROLL_X, SONG_LIST_Y0, SONG_SCROLL_W, SONG_SCROLL_H, up ? t->pal[3] : t->card, false, 0);
    sh_tri(SONG_SCROLL_X + SONG_SCROLL_W / 2.0f, SONG_LIST_Y0 + SONG_SCROLL_H / 2.0f, 6, -1.5708f, rgba(t->ink, up ? 0xFF : 0x40));
    const float dy = SONG_LIST_Y0 + SONG_SCROLL_H + 4;
    sticker(SONG_SCROLL_X, dy, SONG_SCROLL_W, SONG_SCROLL_H, down ? t->pal[3] : t->card, false, 0);
    sh_tri(SONG_SCROLL_X + SONG_SCROLL_W / 2.0f, dy + SONG_SCROLL_H / 2.0f, 6, 1.5708f, rgba(t->ink, down ? 0xFF : 0x40));

    if (app->confirm_name[0]) {  // SAVE AS would replace an existing song
        sh_card(SONG_CONFIRM_X, SONG_CONFIRM_Y, SONG_CONFIRM_W, SONG_CONFIRM_H, rgba(t->pal[0], 0xFF), ink, 4);
        const u32 txt = rgba(ink_on(t, t->pal[0]), 0xFF);
        ui_text(SONG_CONFIRM_X + SONG_CONFIRM_W / 2.0f, SONG_CONFIRM_Y + 6, 0.45f, txt, C2D_AlignCenter, "REPLACE");
        ui_text(SONG_CONFIRM_X + SONG_CONFIRM_W / 2.0f, SONG_CONFIRM_Y + 22, 0.5f, txt, C2D_AlignCenter, "%s?", app->confirm_name);
        ui_text(SONG_CONFIRM_X + SONG_CONFIRM_W / 2.0f, SONG_CONFIRM_Y + 42, 0.36f, txt, C2D_AlignCenter, "hold to replace");
        if (app->song_hold == SONG_HIT_CONFIRM && app->song_hold_t > 0.0f)
            sh_rect(SONG_CONFIRM_X + 3, SONG_CONFIRM_Y + SONG_CONFIRM_H - 7, (SONG_CONFIRM_W - 6) * app->song_hold_t, 4, txt);
    }

    // Buttons. DELETE and NEW (and LOAD over unsaved changes) are held:
    // a bar fills across the sticker while you hold.
    static const char *const labels[] = {"SAVE AS", "SAVE", "LOAD", "DELETE", "NEW"};
    const bool has_sel = app->song_sel >= 0 && app->song_sel < app->song_count;
    for (int b = 0; b < 5; b++) {
        const float y = SONG_BTN_Y0 + b * SONG_BTN_PITCH;
        bool enabled = b == 0 || b == 4 || (b == 1) || ((b == 2 || b == 3) && has_sel);
        // colourful(): never a black or white face, whatever the theme.
        uint32_t face = !enabled ? t->card : colourful(t, b);
        sticker(SONG_BTN_X, y, SONG_BTN_W, SONG_BTN_H, face, false, 0);
        bool hold = b == 3 || b == 4 || (b == 2 && app->song_dirty);
        ui_text(SONG_BTN_X + SONG_BTN_W / 2.0f, y + 2, 0.42f, rgba(ink_on(t, face), enabled ? 0xFF : 0x50), C2D_AlignCenter,
                "%s%s", labels[b], hold && enabled ? " (hold)" : "");
        if (app->song_hold == b && app->song_hold_t > 0.0f)
            sh_rect(SONG_BTN_X + 2, y + SONG_BTN_H - 5, (SONG_BTN_W - 4) * app->song_hold_t, 3, rgba(ink_on(t, face), 0xFF));
    }
}

// --------------------------------------------------------- SEQ page ----

// A die sticker (five pips) that wobbles now and then to invite a tap.
static void draw_die(float x, float y, float w, uint32_t face) {
    const Theme *t = ui_theme();
    sticker(x, y, w, w > TAB_H ? TAB_H : w, face, false, 0);
    const float cx = x + w / 2.0f, cy = y + TAB_H / 2.0f, d = 4.0f;
    const float wob = ((s_frame / 7) % 40 == 0) ? 1.0f : 0.0f;
    const u32 pip = rgba(ink_on(t, face), 0xFF);
    const float px[5] = {-d, d, 0, -d, d}, py[5] = {-d, -d, 0, d, d};
    for (int i = 0; i < 5; i++) sh_circle(cx + px[i] + wob, cy + py[i] - wob, 1.5f, pip);
}

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
    sh_rect(SEQ_BPM_DOWN_X + 4, cy - 1.5f, SEQ_BPM_BTN_W - 8, 3, rgba(ink_on(t, t->pal[1]), 0xFF));
    sh_card(SEQ_BPM_VALUE_X, TAB_Y, SEQ_BPM_VALUE_W, TAB_H, rgba(t->card, 0xFF), ink, 2);
    ui_text(SEQ_BPM_VALUE_X + SEQ_BPM_VALUE_W / 2.0f, TAB_Y + 0.5f, 0.42f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "%d",
            st->bpm);
    sticker(SEQ_BPM_UP_X, TAB_Y, SEQ_BPM_BTN_W, TAB_H, t->pal[1], false, 0);
    u32 plus = rgba(ink_on(t, t->pal[1]), 0xFF);
    sh_rect(SEQ_BPM_UP_X + 4, cy - 1.5f, SEQ_BPM_BTN_W - 8, 3, plus);
    sh_rect(SEQ_BPM_UP_X + SEQ_BPM_BTN_W / 2.0f - 1.5f, cy - 4, 3, 8, plus);

    // Clear: a cross. Dice: five pips.
    sticker(SEQ_CLEAR_X, TAB_Y, SEQ_CLEAR_W, TAB_H, t->card, false, 0);
    sh_bar(SEQ_CLEAR_X + SEQ_CLEAR_W / 2.0f, cy, 12, 3, 0.785f, rgba(t->pal[0], 0xFF));
    sh_bar(SEQ_CLEAR_X + SEQ_CLEAR_W / 2.0f, cy, 12, 3, -0.785f, rgba(t->pal[0], 0xFF));
    draw_die(SEQ_DICE_X, TAB_Y, SEQ_DICE_W, t->pal[3]);
}

// Is a note that started before `step` still held through it?
static bool synth_held(const SeqPattern *p, int part, int step) {
    for (int i = 0; i < p->count; i++)
        if (p->notes[i].part == part && p->notes[i].step < step && p->notes[i].step + p->notes[i].len > step) return true;
    return false;
}

static void draw_grid(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqPattern *p = &app->pattern;
    const SeqStatus *st = &app->seq;
    const u32 ink = rgba(t->ink, 0xFF);
    const float top = SEQ_SYNTH_Y - 3, bottom = SEQ_DRUM_Y + SEQ_LANES * SEQ_DRUM_PITCH + 1;

    sh_card(2, top, BOTTOM_W - 4, bottom - top, rgba(t->card, 0xFF), ink, 3);

    // Beat shading (every other group of four) and the playhead column.
    for (int s = 0; s < SEQ_STEPS; s++) {
        float x = SEQ_GRID_X + s * SEQ_CELL_W;
        if ((s / 4) & 1) sh_rect(x, top + 2, SEQ_CELL_W, bottom - top - 4, rgba(t->ink, 0x12));
        if (st->playing && s == st->step) sh_rect(x, top + 2, SEQ_CELL_W, bottom - top - 4, rgba(t->pal[0], 0x55));
    }

    // Row chips on the left: a squiggle for the synth, key motifs for drums.
    // The synth row belongs to the current part: its chip shows which (tap
    // to cycle).
    const uint32_t pc = part_rgb(t, app->part);
    sh_card(5, SEQ_SYNTH_Y, 15, SEQ_SYNTH_H, rgba(pc, 0xFF), ink, 1.5f);
    ui_text(12.5f, SEQ_SYNTH_Y - 0.5f, 0.42f, rgba(ink_on(t, pc), 0xFF), C2D_AlignCenter, "%d", app->part + 1);
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

        // Accent row: a small triangle that stands up on accented steps.
        if ((p->accents >> s) & 1u)
            sh_tri(x + SEQ_CELL_W / 2.0f, SEQ_ACC_Y + SEQ_ACC_H / 2.0f + 0.5f, 4.5f, -1.5708f, rgba(t->pal[0], 0xFF));
        else
            sh_rect(x + SEQ_CELL_W / 2.0f - 3, SEQ_ACC_Y + SEQ_ACC_H / 2.0f, 6, 1, rgba(t->ink, 0x30));

        int pitches[8];
        int count = seq_notes_at(p, app->part, s, pitches, 8);
        if (count) {
            int low = pitches[0];
            for (int i = 1; i < count && i < 8; i++) low = pitches[i] < low ? pitches[i] : low;
            uint32_t face = colourful(t, 1);
            sh_card(x + 1 + pop, SEQ_SYNTH_Y + pop, SEQ_CELL_W - 2, SEQ_SYNTH_H, rgba(face, 0xFF), ink, head ? 3 : 1.5f);
            ui_text(x + SEQ_CELL_W / 2.0f + pop, SEQ_SYNTH_Y + 1.5f + pop, 0.34f, rgba(ink_on(t, face), 0xFF),
                    C2D_AlignCenter, "%s", kNoteNames[low % 12]);
            if (count > 1) sh_circle(x + SEQ_CELL_W - 4 + pop, SEQ_SYNTH_Y + 3 + pop, 1.6f, ink);
        } else if (synth_held(p, app->part, s)) {  // a longer note started earlier is still sounding here
            sh_rect(x, SEQ_SYNTH_Y + SEQ_SYNTH_H / 2.0f - 1.5f, SEQ_CELL_W, 3, rgba(colourful(t, 1), 0xFF));
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

// Slot stickers A..H (filled ones are coloured; the playing one stands up;
// a queued one blinks), SONG, and the SWING strip.
static void draw_slots(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqStatus *st = &app->seq;
    for (int i = 0; i < SEQ_SLOTS; i++) {
        const float x = SEQ_SLOT_X0 + i * SEQ_SLOT_PITCH;
        const bool cur = st->slot == i, used = (st->used >> i) & 1u;
        const bool queued = st->queued == i && ((s_frame / 8) & 1);
        uint32_t face = cur || queued ? t->pal[i % THEME_PALETTE] : used ? rgb_lerp(t->card, t->pal[i % THEME_PALETTE], 0.35f) : t->card;
        char label[2] = {(char)('A' + i), 0};
        sticker(x, SEQ_SLOT_Y, SEQ_SLOT_W, SEQ_SLOT_H, face, !cur, label);
        if (app->slot_hold == i && app->slot_hold_t > 0.0f)  // hold to copy here
            sh_rect(x + 1, SEQ_SLOT_Y + SEQ_SLOT_H - 3, (SEQ_SLOT_W - 2) * app->slot_hold_t, 3, rgba(ink_on(t, face), 0xFF));
    }
    sticker(SEQ_SONG_X, SEQ_SLOT_Y, SEQ_SONG_W, SEQ_SLOT_H, st->song ? t->pal[3] : t->card, !st->song, "SONG");
    sticker(SEQ_ROLL_X, SEQ_SLOT_Y, SEQ_ROLL_W, SEQ_SLOT_H, app->roll_view ? t->pal[1] : t->card, !app->roll_view, "ROLL");

    sh_card(SEQ_SWING_X, SEQ_SLOT_Y, SEQ_SWING_W, SEQ_SLOT_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 2);
    float fill = (SEQ_SWING_W - 4) * st->swing / SEQ_SWING_MAX;
    sh_rect_pattern(SEQ_SWING_X + 2, SEQ_SLOT_Y + 2, fill, SEQ_SLOT_H - 4, rgba(t->pal[2], 0xFF), PAT_ZIGZAG,
                    rgba(t->ink, 0x40));
    ui_text(SEQ_SWING_X + SEQ_SWING_W / 2.0f, SEQ_SLOT_Y + 1, 0.38f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter,
            "SW %d%%", (int)(st->swing / SEQ_SWING_MAX * 100.0f + 0.5f));
}

// The piano roll: pitches up the side, steps across, notes as sticker bars
// as long as they sound.
static void draw_roll(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqPattern *p = &app->pattern;
    const SeqStatus *st = &app->seq;
    const u32 ink = rgba(t->ink, 0xFF);
    const bool chrom = app->roll_chromatic;
    const float gx = ROLL_X0, gy = ROLL_Y0, gw = SEQ_STEPS * ROLL_CELL_W, gh = ROLL_ROWS * ROLL_ROW_H;
    const int rows = music_roll_rows(chrom);

    sh_card(2, ROLL_TOGGLE_Y - 2, BOTTOM_W - 4, gy + gh + 3 - (ROLL_TOGGLE_Y - 2), rgba(t->card, 0xFF), ink, 3);

    // Scale / chromatic toggle.
    uint32_t tf = chrom ? t->pal[0] : t->pal[1];
    sh_card(ROLL_TOGGLE_X, ROLL_TOGGLE_Y, ROLL_TOGGLE_W, ROLL_TOGGLE_H, rgba(tf, 0xFF), ink, 0);
    ui_text(ROLL_TOGGLE_X + ROLL_TOGGLE_W / 2.0f, ROLL_TOGGLE_Y - 1.5f, 0.34f, rgba(ink_on(t, tf), 0xFF), C2D_AlignCenter,
            chrom ? "#" : "C");
    for (int k = 0; k < PARTS; k++) {  // which part the roll edits
        const bool on = app->part == k, muted = (st->muted >> k) & 1u;
        const uint32_t f = on ? part_rgb(t, k) : t->card;
        const float px = ROLL_PART_X0 + k * ROLL_PART_PITCH;
        sh_card(px, ROLL_TOGGLE_Y, ROLL_PART_W, ROLL_TOGGLE_H, rgba(f, 0xFF), ink, on ? 1.5f : 0.0f);
        ui_text(px + ROLL_PART_W / 2.0f, ROLL_TOGGLE_Y - 1.5f, 0.34f, rgba(ink_on(t, f), muted ? 0x60 : 0xFF), C2D_AlignCenter,
                "%d", k + 1);
    }
    ui_text(ROLL_PART_X0 + PARTS * ROLL_PART_PITCH + 4, ROLL_TOGGLE_Y - 1, 0.32f, rgba(ink_on(t, t->card), 0x90), 0,
            "%s  |  tap add/remove, drag move/stretch", chrom ? "CHROMATIC" : "C MAJOR");

    // Rows: shading, and the keyboard strip with C labels.
    for (int r = 0; r < ROLL_ROWS; r++) {
        int abs_row = app->roll_scroll + r;
        if (abs_row >= rows) break;
        int pitch = music_roll_pitch(abs_row, chrom);
        float y = gy + (ROLL_ROWS - 1 - r) * ROLL_ROW_H;
        bool sharp = !music_in_scale(pitch);
        bool live = (app->live_pitch[pitch >> 3] >> (pitch & 7)) & 1u;
        if (r & 1) sh_rect(gx, y, gw, ROLL_ROW_H, rgba(t->ink, 0x0C));
        if (live) sh_rect(gx, y, gw, ROLL_ROW_H, rgba(part_rgb(t, app->part), 0x40));  // the row you're playing
        if (pitch % 12 == 0) sh_rect(gx, y + ROLL_ROW_H - 1, gw, 1, rgba(t->ink, 0x50));
        sh_rect(ROLL_KEYS_X, y, ROLL_KEYS_W, ROLL_ROW_H - 1,
                live ? rgba(part_rgb(t, app->part), 0xFF) : rgba(sharp ? t->ink : 0xFFFFFF, 0xFF));
        sh_rect(ROLL_KEYS_X, y + ROLL_ROW_H - 1, ROLL_KEYS_W, 1, rgba(t->ink, 0x60));
        if (pitch % 12 == 0)
            ui_text(ROLL_KEYS_X + 1, y - 2, 0.28f, rgba(t->ink, 0xFF), 0, "%s", music_note_name(pitch));
    }
    // Beat shading and the playhead.
    for (int s2 = 0; s2 < SEQ_STEPS; s2++) {
        float x = gx + s2 * ROLL_CELL_W;
        if ((s2 / 4) & 1) sh_rect(x, gy, ROLL_CELL_W, gh, rgba(t->ink, 0x0E));
        sh_rect(x, gy, 1, gh, rgba(t->ink, s2 % 4 == 0 ? 0x40 : 0x14));
        if (st->playing && s2 == st->step) sh_rect(x, gy, ROLL_CELL_W, gh, rgba(t->pal[0], 0x40));
    }
    // Other parts' notes, faintly, to write against.
    for (int i = 0; i < p->count; i++) {
        const SeqRollNote *n = &p->notes[i];
        if (n->part == app->part) continue;
        int r = music_roll_row(n->pitch, chrom) - app->roll_scroll;
        if (r < 0 || r >= ROLL_ROWS) continue;
        float x = gx + n->step * ROLL_CELL_W + 1, y = gy + (ROLL_ROWS - 1 - r) * ROLL_ROW_H;
        float w = n->len * ROLL_CELL_W - 2;
        if (x + w > gx + gw) w = gx + gw - x;
        sh_rect(x, y + 2, w, ROLL_ROW_H - 5, rgba(part_rgb(t, n->part), 0x55));
    }
    // This part's notes.
    for (int i = 0; i < p->count; i++) {
        const SeqRollNote *n = &p->notes[i];
        if (n->part != app->part) continue;
        int r = music_roll_row(n->pitch, chrom) - app->roll_scroll;
        if (r < 0 || r >= ROLL_ROWS) continue;
        float x = gx + n->step * ROLL_CELL_W + 1, y = gy + (ROLL_ROWS - 1 - r) * ROLL_ROW_H;
        float w = n->len * ROLL_CELL_W - 2;
        bool over = x + w > gx + gw;  // runs past the bar: clip and mark it
        if (over) w = gx + gw - x;
        bool sounding = st->playing && st->step >= n->step && st->step < n->step + n->len;
        bool active = app->roll_active == i;
        uint32_t face = t->pal[(n->pitch / 12) % THEME_PALETTE];
        float lift = sounding ? -1.0f : 0.0f;
        sh_card(x + lift, y + lift, w, ROLL_ROW_H - 1, rgba(face, 0xFF), ink, active ? 2.5f : sounding ? 2.0f : 1.0f);
        if (over) sh_tri(gx + gw - 3, y + ROLL_ROW_H / 2.0f - 0.5f, 3, 0.0f, rgba(ink_on(t, face), 0xFF));
        if (!chrom && !music_in_scale(n->pitch))  // shown on the row below its pitch
            ui_text(x + 2 + lift, y - 2.5f + lift, 0.26f, rgba(ink_on(t, face), 0xFF), 0, "#");
    }
    // Scroll hints at the strip's ends.
    if (app->roll_scroll + ROLL_ROWS < rows) sh_tri(ROLL_KEYS_X + ROLL_KEYS_W / 2.0f, gy - 2, 3, -1.5708f, rgba(t->pal[2], 0xFF));
    if (app->roll_scroll > 0) sh_tri(ROLL_KEYS_X + ROLL_KEYS_W / 2.0f, gy + gh + 1, 3, 1.5708f, rgba(t->pal[2], 0xFF));
}

static void draw_seq_page(const AppState *app) {
    draw_transport(app);
    if (app->roll_view)
        draw_roll(app);
    else
        draw_grid(app);
    draw_slots(app);
}

// ------------------------------------------------------- SYNTH page ----

// A sound's mascot: its motif with googly eyes, squishing on every note.
static void draw_mascot(int sound, float x, float y, float size, float look_x, float look_y, uint32_t rgb) {
    const Theme *t = ui_theme();
    const Sound *s = sound_get(sound);
    float squish = 1.0f + ui_bump() * 0.25f;
    Motif m = {0};
    m.kind = s->motif;
    m.x = x;
    m.y = y;
    m.size = size * squish;
    m.angle = -1.5708f + sinf(s_frame * 0.05f + sound) * 0.15f;
    m.rgb = rgb;
    m.seed = (uint32_t)sound * 31u + 7u;
    motif_draw(&m, 0, 1.0f, 0xFF, t->ink, 2.0f);
    bool blink = ((s_frame + sound * 37) % 190) < 6;
    sh_eyes(x, y - size * 0.05f, size * 0.22f, look_x, look_y, blink, rgba(t->ink, 0xFF));
}

static void draw_sound_tiles(const AppState *app) {
    const Theme *t = ui_theme();
    for (int i = 0; i < sound_count(); i++) {
        const bool on = app->sound == i;
        const float x = SYN_TILE_X0 + (i % SYN_TILE_COLS) * SYN_TILE_PITCH_X;
        const float y = SYN_TILE_Y0 + (i / SYN_TILE_COLS) * SYN_TILE_PITCH_Y;
        const uint32_t face = on ? t->pal[i % THEME_PALETTE] : t->card;
        sh_card(x, y, SYN_TILE_W, SYN_TILE_H, rgba(face, 0xFF), rgba(t->ink, 0xFF), on ? 3.0f : 1.5f);
        uint32_t body = on ? t->card : t->pal[i % THEME_PALETTE];
        float bob = on ? sinf(s_frame * 0.15f) * 1.2f : 0.0f;
        draw_mascot(i, x + 12, y + SYN_TILE_H / 2.0f + bob, on ? 8.5f : 7.0f, 0, 0, body);
        ui_text(x + 33, y + 5, 0.36f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter, "%s", sound_get(i)->tag);
    }
}

static void draw_bank_grid(const AppState *app, const Sound *s) {
    const Theme *t = ui_theme();
    const int cur = app->patch - s->bank_first;
    ui_text(SYN_PAD_X + 6, SYN_PAD_Y + 2, 0.4f, rgba(ink_on(t, t->card), 0xFF), 0, "%s  %d PATCHES", s->name, s->bank_count);
    ui_text(SYN_DICE_X - 4, SYN_PAD_Y + 2, 0.36f, rgba(ink_on(t, t->card), 0x90), C2D_AlignRight, "tap a dot");
    for (int i = 0; i < s->bank_count && i < BANK_COLS * BANK_ROWS; i++) {
        float cx = BANK_X0 + (i % BANK_COLS) * BANK_CELL_W + BANK_CELL_W / 2.0f;
        float cy = BANK_Y0 + (i / BANK_COLS) * BANK_CELL_H + BANK_CELL_H / 2.0f;
        uint32_t col = t->pal[(i / BANK_COLS) % THEME_PALETTE];
        if (i == cur) {
            float r = 6.0f + ui_bump() * 2.0f;
            sh_circle(cx + 1.5f, cy + 1.5f, r, rgba(t->ink, 0xFF));
            sh_circle(cx, cy, r, rgba(col, 0xFF));
            sh_ring(cx, cy, r + 0.5f, 1.5f, rgba(t->ink, 0xFF));
        } else {
            sh_circle(cx, cy, 2.6f, rgba(col, 0xD0));
        }
    }
}

static void draw_creature_pad(const AppState *app, const Sound *s) {
    const Theme *t = ui_theme();
    const u32 ink = rgba(t->ink, 0xFF);
    const float x0 = SYN_PAD_X + 8, y0 = SYN_PAD_Y + 20, w = SYN_PAD_W - 16, h = SYN_PAD_H - 28;
    sh_grid(x0, y0, w, h, 13, rgba(t->ink, 0x16));
    ui_text(SYN_PAD_X + 6, SYN_PAD_Y + 2, 0.4f, rgba(ink_on(t, t->card), 0xFF), 0, "%s", s->name);
    if (app->sound != sound_mic_index())
        ui_text(SYN_DICE_X - 4, SYN_PAD_Y + 3, 0.32f, rgba(ink_on(t, t->card), 0x90), C2D_AlignRight, "%s", s->blurb);

    // MIC: the take itself is the backdrop, with START marked on it.
    if (app->sound == sound_mic_index()) {
        const float mid = y0 + h / 2.0f, bw = w / SAMPLER_OVERVIEW;
        for (int i = 0; i < SAMPLER_OVERVIEW; i++) {
            float a = app->take_overview[i] * (h / 2.0f - 4.0f);
            bool played = (float)i / SAMPLER_OVERVIEW >= app->macro[0] * 0.8f;
            sh_rect(x0 + i * bw, mid - a, bw * 0.7f, 2 * a + 1, rgba(t->pal[played ? 2 : 4], played ? 0x90 : 0x40));
        }
        sh_rect(x0 + app->macro[0] * 0.8f * w - 1, y0, 2, h, rgba(t->pal[0], 0xFF));
        // REC sticker: hold it to record.
        uint32_t face = app->mic_rec ? (((s_frame / 8) & 1) ? t->pal[0] : t->card) : t->card;
        sticker(SYN_REC_X, SYN_DICE_Y, SYN_REC_W, 16, face, app->mic_rec, 0);
        float o = app->mic_rec ? 2.0f : 0.0f;
        sh_circle(SYN_REC_X + 8 + o, SYN_DICE_Y + 8 + o, 4.0f, rgba(app->mic_ok ? t->pal[0] : t->ink, 0xFF));
        ui_text(SYN_REC_X + 24 + o, SYN_DICE_Y + o, 0.36f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter,
                app->mic_ok ? "REC" : "N/A");
    }

    const float cx = x0 + app->macro[0] * w, cy = y0 + (1.0f - app->macro[1]) * h;
    // Crosshair to the axes, as dashed lines in the axis colours.
    for (float yy = cy + 6; yy < y0 + h; yy += 6) sh_rect(cx - 0.75f, yy, 1.5f, 3, rgba(t->pal[0], 0xC0));
    for (float xx = x0; xx < cx - 6; xx += 6) sh_rect(xx, cy - 0.75f, 3, 1.5f, rgba(t->pal[1], 0xC0));

    // Axis labels with little arrow triangles.
    uint32_t lab = ink_on(t, t->card);
    ui_text(x0 + w - 8, y0 + h - 1, 0.36f, rgba(lab, 0xFF), C2D_AlignRight, "%s %d", s->macro_name[0],
            (int)(app->macro[0] * 100.0f + 0.5f));
    sh_tri(x0 + w - 3, y0 + h + 5, 4, 0.0f, rgba(t->pal[0], 0xFF));
    sh_tri(x0 + 4, y0 + 3, 4, -1.5708f, rgba(t->pal[1], 0xFF));
    ui_text(x0 + 10, y0 - 3, 0.36f, rgba(lab, 0xFF), 0, "%s %d", s->macro_name[1], (int)(app->macro[1] * 100.0f + 0.5f));

    // The creature itself; its eyes follow the stylus while you drag.
    float lx = 0, ly = 0;
    if (!app->pad_touch) {
        lx = sinf(s_frame * 0.031f) * 0.8f;
        ly = cosf(s_frame * 0.023f) * 0.5f;
    }
    draw_mascot(app->sound, cx, cy, app->pad_touch ? 17.0f : 15.0f, lx, ly, t->pal[(app->sound + 1) % THEME_PALETTE]);
    (void)ink;
}

// Part stickers in the tab row: tap to pick, hold to mute.
static void draw_part_tabs(const AppState *app) {
    const Theme *t = ui_theme();
    for (int k = 0; k < PARTS; k++) {
        const bool on = app->part == k, muted = (app->seq.muted >> k) & 1u;
        const int sound = on ? app->sound : app->parts[k].sound;
        const uint32_t face = on ? part_rgb(t, k) : t->card;
        const float x = PART_TAB_X0 + k * PART_TAB_PITCH;
        char label[16];
        snprintf(label, sizeof(label), "%d %s", k + 1, sound_get(sound)->tag);
        sticker(x, TAB_Y, PART_TAB_W, TAB_H, face, !on, label);
        if (muted) {  // muted: struck through
            float o = on ? 0.0f : 2.0f;
            sh_rect(x + 4 + o, TAB_Y + TAB_H / 2.0f + o, PART_TAB_W - 8, 2, rgba(t->pal[0], 0xFF));
        }
        if (app->part_hold == k && app->part_hold_t > 0.0f)
            sh_rect(x + 2, TAB_Y + TAB_H - 4, (PART_TAB_W - 4) * app->part_hold_t, 2.5f, rgba(ink_on(t, face), 0xFF));
    }
}

static void draw_synth_page(const AppState *app) {
    const Theme *t = ui_theme();
    const Sound *s = sound_get(app->sound);
    draw_part_tabs(app);
    draw_sound_tiles(app);
    sh_card(SYN_PAD_X, SYN_PAD_Y, SYN_PAD_W, SYN_PAD_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 3);
    if (s->kind == SOUND_BANK)
        draw_bank_grid(app, s);
    else
        draw_creature_pad(app, s);
    draw_die(SYN_DICE_X, SYN_DICE_Y, SYN_DICE_W, t->pal[3]);
}

// ---------------------------------------------------------- FX page ----

static const uint8_t kFxMotif[FX_COUNT] = {MO_DOTS, MO_SQUIGGLE, MO_ZIGZAG, MO_RING};

static void draw_fx_stickers(const AppState *app) {
    const Theme *t = ui_theme();
    for (int i = 0; i < FX_COUNT; i++) {
        const bool sel = app->fx_sel == i, live = app->fx.on[i];
        const float y = FX_STICK_Y0 + i * FX_STICK_PITCH;
        const uint32_t face = sel ? t->pal[(i + 1) % THEME_PALETTE] : t->card;
        sticker(FX_STICK_X, y, FX_STICK_W, FX_STICK_H, face, false, 0);
        Motif m = {0};
        m.kind = kFxMotif[i];
        m.x = FX_STICK_X + 13;
        m.y = y + FX_STICK_H / 2.0f;
        m.size = 6.5f;
        m.angle = live ? s_frame * 0.2f : 0.0f;
        m.rgb = sel ? ink_on(t, face) : t->pal[(i + 1) % THEME_PALETTE];
        motif_draw(&m, 0, 1.0f, 0xFF, t->ink, 0);
        ui_text(FX_STICK_X + 25, y + 3, 0.4f, rgba(ink_on(t, face), 0xFF), 0, "%s", fx_name((FxKind)i));
        if (live) sh_circle(FX_STICK_X + FX_STICK_W - 7, y + 6, 3.0f + ((s_frame >> 3) & 1), rgba(t->pal[0], 0xFF));
    }
    uint32_t hold_face = app->fx_hold ? t->pal[0] : t->card;
    sticker(FX_STICK_X, FX_HOLD_Y, FX_STICK_W, FX_HOLD_H, hold_face, app->fx_hold, app->fx_hold ? "HOLD: ON" : "HOLD");
}

// Each effect gets its own pad backdrop that explains its axes.
static void draw_fx_backdrop(FxKind k) {
    const Theme *t = ui_theme();
    const float x = FX_PAD_X + 2, y = FX_PAD_Y + 2, w = FX_PAD_W - 4, h = FX_PAD_H - 4;
    const uint32_t lab = ink_on(t, t->card);
    switch (k) {
        case FX_CRUSH:  // pixels get chunkier to the right, coarser upwards
            for (int gy = 0; gy < 8; gy++)
                for (int gx = 0; gx < 14; gx++)
                    if ((gx + gy) & 1) sh_rect(x + gx * w / 14, y + gy * h / 8, w / 14, h / 8, rgba(t->pal[gx % THEME_PALETTE], 0x16 + gx * 2));
            ui_text(x + 4, y + h - 13, 0.34f, rgba(lab, 0xA0), 0, "smooth");
            ui_text(x + w - 4, y + h - 13, 0.34f, rgba(lab, 0xA0), C2D_AlignRight, "chunky");
            break;
        case FX_SWEEP:  // dark low-pass on the left, bright high-pass on the right
            C2D_DrawRectangle(x, y, 0.5f, w / 2, h, rgba(t->ink, 0x50), rgba(t->ink, 0x00), rgba(t->ink, 0x50), rgba(t->ink, 0x00));
            C2D_DrawRectangle(x + w / 2, y, 0.5f, w / 2, h, rgba(t->pal[1], 0x00), rgba(t->pal[1], 0x60), rgba(t->pal[1], 0x00), rgba(t->pal[1], 0x60));
            sh_rect(x + w * 0.48f, y, w * 0.04f, h, rgba(t->ink, 0x18));
            ui_text(x + 4, y + h - 13, 0.34f, rgba(lab, 0xC0), 0, "LOW");
            ui_text(x + w - 4, y + h - 13, 0.34f, rgba(lab, 0xC0), C2D_AlignRight, "HIGH");
            ui_text(x + w / 2, y + h - 13, 0.3f, rgba(lab, 0x80), C2D_AlignCenter, "off");
            break;
        case FX_STUTTER: {  // five tempo zones
            static const char *const zones[] = {"1/2", "1/4", "1/8", "1/16", "1/32"};
            for (int z = 0; z < 5; z++) {
                if (z & 1) sh_rect(x + z * w / 5, y, w / 5, h, rgba(t->pal[2], 0x28));
                ui_text(x + (z + 0.5f) * w / 5, y + h - 13, 0.34f, rgba(lab, 0xB0), C2D_AlignCenter, "%s", zones[z]);
            }
            break;
        }
        default:  // TAPE: two reels
            sh_ring(x + w * 0.3f, y + h * 0.45f, 26, 4, rgba(t->ink, 0x1C));
            sh_ring(x + w * 0.7f, y + h * 0.45f, 26, 4, rgba(t->ink, 0x1C));
            ui_text(x + 4, y + h - 13, 0.34f, rgba(lab, 0xA0), 0, "quick stop");
            ui_text(x + w - 4, y + h - 13, 0.34f, rgba(lab, 0xA0), C2D_AlignRight, "slow stop");
            break;
    }
    ui_text(x + 4, y + 1, 0.34f, rgba(lab, 0xB0), 0, "%s", fx_axis_name(k, 1));
}

static void draw_fx_pad(const AppState *app) {
    const Theme *t = ui_theme();
    const FxKind k = (FxKind)app->fx_sel;
    sh_card(FX_PAD_X, FX_PAD_Y, FX_PAD_W, FX_PAD_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 3);
    draw_fx_backdrop(k);

    const float m = 8.0f, iw = FX_PAD_W - 2 * m, ih = FX_PAD_H - 2 * m;
    Motif mo = {0};
    mo.kind = kFxMotif[k];
    mo.seed = 99u;
    // Trail of fading motifs behind the stylus.
    for (int i = s_trail_len - 1; i >= 1; i--) {
        mo.x = FX_PAD_X + m + s_trail_x[i] * iw;
        mo.y = FX_PAD_Y + m + (1.0f - s_trail_y[i]) * ih;
        mo.size = 9.0f - i * 0.5f;
        mo.angle = s_frame * 0.1f + i;
        mo.rgb = t->pal[i % THEME_PALETTE];
        motif_draw(&mo, 0, 1.0f, (uint8_t)(220 - i * 14), t->ink, 0);
    }
    // The cursor: where the effect sits now (live or held).
    if (app->fx.on[k] || app->pad_touch) {
        float px = app->pad_touch ? app->pad_x : app->fx_x[k], py = app->pad_touch ? app->pad_y : app->fx_y[k];
        mo.x = FX_PAD_X + m + px * iw;
        mo.y = FX_PAD_Y + m + (1.0f - py) * ih;
        mo.size = 15.0f + ui_bump() * 4.0f;
        mo.angle = s_frame * 0.08f;
        mo.rgb = t->pal[(k + 1) % THEME_PALETTE];
        motif_draw(&mo, 0, 1.0f, 0xFF, t->ink, 3.0f);
    } else {
        ui_text(FX_PAD_X + FX_PAD_W / 2.0f, FX_PAD_Y + FX_PAD_H / 2.0f - 8, 0.45f, rgba(ink_on(t, t->card), 0x90),
                C2D_AlignCenter, "touch to %s", fx_name(k));
    }
}

static void draw_fx_page(const AppState *app) {
    draw_fx_stickers(app);
    draw_fx_pad(app);
}

// -------------------------------------------------------- TAPE page ----

// A tape reel: a ring with three spokes, turning with the tape.
static void draw_reel(float x, float y, float r, float angle, uint32_t rgb) {
    const Theme *t = ui_theme();
    sh_ring(x, y, r, 2.5f, rgba(t->ink, 0xFF));
    for (int i = 0; i < 3; i++) sh_bar(x, y, r * 1.6f, 2.0f, angle + i * 2.094f, rgba(rgb, 0xFF));
    sh_circle(x, y, r * 0.3f, rgba(t->ink, 0xFF));
}

static void draw_tape_tracks(const AppState *app) {
    const Theme *t = ui_theme();
    const TapeStatus *tp = &app->tape;
    for (int k = 0; k < TAPE_TRACKS; k++) {
        const float y = TAPE_TRK_Y0 + k * TAPE_TRK_PITCH;
        const bool armed = tp->track == k;
        const uint32_t face = armed ? t->pal[k % THEME_PALETTE] : t->card;
        sticker(TAPE_TRK_X, y, TAPE_TRK_W, TAPE_TRK_H, face, false, 0);
        const uint32_t ink = ink_on(t, face);
        ui_text(TAPE_TRK_X + 4, y + 3, 0.45f, rgba(ink, 0xFF), 0, "%d", k + 1);
        // Source chip: what this track records (tap to change).
        const int src = tp->source[k];
        const uint32_t sface = src == TAPE_SRC_ALL ? t->card : strip_rgb(t, src);
        const u32 stext = rgba(ink_on(t, sface), 0xFF);
        const float sx = TAPE_TRK_X + TAPE_SRC_DX, scx = sx + TAPE_SRC_W / 2.0f;
        sh_card(sx, y + 3, TAPE_SRC_W, 13, rgba(sface, 0xFF), rgba(t->ink, 0xFF), 0);
        if (src == TAPE_SRC_ALL)
            ui_text(scx, y + 3.5f, 0.32f, stext, C2D_AlignCenter, "ALL");
        else if (src == MIX_DRUMS)
            ui_text(scx, y + 3.5f, 0.32f, stext, C2D_AlignCenter, "DRUMS");
        else
            ui_text(scx, y + 3.5f, 0.32f, stext, C2D_AlignCenter, "PART %d", src + 1);
        // Level meter along the bottom (dim while empty), and a "recording" dot.
        float lv = tp->level[k] * TAPE_SRC_W;
        sh_rect(sx, y + 18, TAPE_SRC_W, 3, rgba(t->ink, tp->used[k] ? 0x30 : 0x12));
        sh_rect(sx, y + 18, lv > TAPE_SRC_W ? TAPE_SRC_W : lv, 3, rgba(armed ? ink : t->pal[k % THEME_PALETTE], 0xFF));
        if (armed && tp->recording) sh_circle(TAPE_TRK_X + 7, y + 18, 2.5f, rgba(t->pal[0], 0xFF));
        // Mute stamp.
        const float mx = TAPE_TRK_X + TAPE_TRK_W - TAPE_MUTE_W;
        sh_rect(mx, y + 2, 1, TAPE_TRK_H - 4, rgba(t->ink, 0x60));
        ui_text(mx + TAPE_MUTE_W / 2.0f, y + 3, 0.42f, rgba(tp->muted[k] ? t->pal[0] : ink, tp->muted[k] ? 0xFF : 0x70),
                C2D_AlignCenter, "M");
        // Hold-to-wipe progress: a bar that fills across the sticker.
        if (app->tape_hold_track == k && app->tape_hold > 0.0f)
            sh_rect(TAPE_TRK_X + 2, y + TAPE_TRK_H - 5, (TAPE_TRK_W - 4) * app->tape_hold, 3, rgba(ink_on(t, face), 0xFF));
    }
    uint32_t lf = app->tape_link ? t->pal[3] : t->card;
    sticker(TAPE_TRK_X, TAPE_LINK_Y, TAPE_TRK_W, TAPE_LINK_H, lf, false, app->tape_link ? "SEQ LINK" : "NO LINK");
}

static void draw_tape_window(const AppState *app) {
    const Theme *t = ui_theme();
    const TapeStatus *tp = &app->tape;
    const float x0 = TAPE_WIN_X, y0 = TAPE_WIN_Y, w = TAPE_WIN_W, h = TAPE_WIN_H;
    sh_card(x0, y0, w, h, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 3);
    if (!tp->ok) {
        ui_text(x0 + w / 2, y0 + h / 2 - 8, 0.45f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "no memory for tape");
        return;
    }
    const float lane_h = (h - 6) / TAPE_TRACKS, px_per_s = w / TAPE_WIN_SECONDS;
    for (int k = 0; k < TAPE_TRACKS; k++) {
        const float mid = y0 + 3 + lane_h * (k + 0.5f);
        if (tp->track == k) sh_rect(x0 + 2, mid - lane_h / 2, w - 4, lane_h, rgba(t->pal[k % THEME_PALETTE], 0x22));
        const uint8_t a = tp->muted[k] ? 0x40 : 0xE0;
        // The lanes scroll past a fixed playhead in the middle.
        for (float px = 3; px < w - 3; px += 2) {
            float secs = tp->seconds + (px - w / 2) / px_per_s;
            int block = (int)floorf(secs * TAPE_RATE / TAPE_PEAK_BLOCK);
            float pk = tape_peak(k, block) * (lane_h / 2 - 1);
            if (pk >= 0.5f) sh_rect(x0 + px, mid - pk, 1.5f, pk * 2, rgba(t->pal[k % THEME_PALETTE], a));
        }
        sh_rect(x0 + 3, mid, w - 6, 1, rgba(t->ink, 0x18));
    }
    // Second ticks and the playhead.
    for (int sx = -4; sx <= 4; sx++) {
        float secs = floorf(tp->seconds) + sx;
        float px = w / 2 + (secs - tp->seconds) * px_per_s;
        if (px > 2 && px < w - 2) sh_rect(x0 + px, y0 + 2, 1, 4, rgba(t->ink, 0x80));
    }
    sh_rect(x0 + w / 2 - 1, y0 + 2, 2, h - 4, rgba(tp->recording ? t->pal[0] : t->ink, 0xFF));
    sh_tri(x0 + w / 2, y0 + 5, 4, 1.5708f, rgba(tp->recording ? t->pal[0] : t->ink, 0xFF));
}

static void draw_tape_transport(const AppState *app) {
    const Theme *t = ui_theme();
    const TapeStatus *tp = &app->tape;
    const float cy = TAPE_BTN_Y + TAPE_BTN_H / 2.0f;

    sticker(TAPE_REW_X, TAPE_BTN_Y, TAPE_BTN_W, TAPE_BTN_H, t->card, false, 0);
    u32 ic = rgba(ink_on(t, t->card), 0xFF);
    sh_rect(TAPE_REW_X + 7, cy - 5, 2.5f, 10, ic);
    sh_tri(TAPE_REW_X + 15, cy, 5, 3.14159f, ic);
    sh_tri(TAPE_REW_X + 23, cy, 5, 3.14159f, ic);

    uint32_t pf = tp->playing ? t->pal[3] : t->card;
    sticker(TAPE_PLAY_X, TAPE_BTN_Y, TAPE_BTN_W, TAPE_BTN_H, pf, false, 0);
    ic = rgba(ink_on(t, pf), 0xFF);
    if (tp->playing)
        sh_rect(TAPE_PLAY_X + TAPE_BTN_W / 2.0f - 5, cy - 5, 10, 10, ic);
    else
        sh_tri(TAPE_PLAY_X + TAPE_BTN_W / 2.0f + 1, cy, 7, 0.0f, ic);

    const bool armed = app->tape.recording || s_tape_armed_ui;
    uint32_t rf = armed ? (tp->recording && ((s_frame / 12) & 1) ? t->pal[0] : t->card) : t->card;
    sticker(TAPE_REC_X, TAPE_BTN_Y, TAPE_BTN_W, TAPE_BTN_H, rf, armed, 0);
    // The dot flips to ink when the blinking face is the dot's own colour.
    sh_circle(TAPE_REC_X + TAPE_BTN_W / 2.0f + (armed ? 2 : 0), cy + (armed ? 2 : 0), 5.5f,
              rgba(armed ? (rf == t->pal[0] ? t->ink : t->pal[0]) : t->ink, 0xFF));

    // Counter, with two reels that turn at the tape's speed.
    sh_card(TAPE_TIME_X, TAPE_BTN_Y, TAPE_TIME_W, TAPE_BTN_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 2);
    int whole = (int)tp->seconds;
    ui_text(TAPE_TIME_X + 52, TAPE_BTN_Y + 2, 0.5f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "%02d:%02d.%d",
            whole / 60, whole % 60, (int)((tp->seconds - whole) * 10));
    static float reel;
    reel += tp->speed * 0.12f;
    draw_reel(TAPE_TIME_X + TAPE_TIME_W - 30, cy, 8, reel, t->pal[1]);
    draw_reel(TAPE_TIME_X + TAPE_TIME_W - 11, cy, 8, reel * 0.8f, t->pal[2]);

    // Speed strip: reverse on the left, stop in the middle, normal at 3/4.
    const float sx = TAPE_WIN_X, sw = TAPE_WIN_W;
    sh_card(sx, TAPE_SPEED_Y, sw, TAPE_SPEED_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 2);
    C2D_DrawRectangle(sx + 2, TAPE_SPEED_Y + 2, 0.5f, sw / 2 - 2, TAPE_SPEED_H - 4, rgba(t->pal[0], 0x50),
                      rgba(t->pal[0], 0x00), rgba(t->pal[0], 0x50), rgba(t->pal[0], 0x00));
    C2D_DrawRectangle(sx + sw / 2, TAPE_SPEED_Y + 2, 0.5f, sw / 2 - 2, TAPE_SPEED_H - 4, rgba(t->pal[1], 0x00),
                      rgba(t->pal[1], 0x50), rgba(t->pal[1], 0x00), rgba(t->pal[1], 0x50));
    static const char *const marks[] = {"REV x2", "REV", "STOP", "x1", "x2"};
    for (int m = 0; m < 5; m++) {
        float mx = sx + 6 + (sw - 12) * m / 4.0f;
        sh_rect(mx - 0.5f, TAPE_SPEED_Y + 2, 1, 4, rgba(t->ink, 0x80));
        ui_text(mx + (m == 0 ? 14 : m == 4 ? -8 : 0), TAPE_SPEED_Y + 6, 0.3f, rgba(ink_on(t, t->card), 0x90),
                C2D_AlignCenter, "%s", marks[m]);
    }
    float knob = sx + 6 + (sw - 12) * (s_tape_speed_ui + 2.0f) / 4.0f;
    sh_tri(knob + 1.5f, TAPE_SPEED_Y + 3.5f, 6, 1.5708f, rgba(t->ink, 0xFF));
    sh_tri(knob, TAPE_SPEED_Y + 2, 6, 1.5708f, rgba(t->pal[2], 0xFF));
}

static void draw_tape_page(const AppState *app) {
    draw_tape_tracks(app);
    draw_tape_window(app);
    draw_tape_transport(app);
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
    switch (app->page) {
        case PAGE_SEQ: draw_seq_page(app); break;
        case PAGE_SYNTH: draw_synth_page(app); break;
        case PAGE_MIX: draw_mix_page(app); break;
        case PAGE_FX: draw_fx_page(app); break;
        case PAGE_TAPE: draw_tape_page(app); break;
        case PAGE_SONGS: draw_songs_page(app); break;
        default: draw_sound_page(app); break;
    }
    for (int k = 0; k < MUSIC_KEYS; k++) draw_key(app, k);
    if (app->notice_frames > 0 && !app->busy) {  // something went wrong (or needs saying)
        sh_card(40, 84, 240, 40, rgba(t->pal[0], 0xFF), rgba(t->ink, 0xFF), 4);
        ui_text(160, 96, 0.45f, rgba(ink_on(t, t->pal[0]), 0xFF), C2D_AlignCenter, "%s", app->notice);
    }
    if (app->busy) {  // a slow job is about to run: say so
        sh_card(70, 80, 180, 50, rgba(t->pal[1], 0xFF), rgba(t->ink, 0xFF), 4);
        sh_floppy(84, 92, 26, rgba(t->pal[4], 0xFF), rgba(t->card, 0xFF), rgba(t->ink, 0xFF));
        ui_text(180, 94, 0.6f, rgba(ink_on(t, t->pal[1]), 0xFF), C2D_AlignCenter, "%s", app->busy);
    }
}
