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

static const char *const kKnobShort[MIX_KNOBS] = {"DRV", "CRSH", "TONE", "PAN", "ECHO", "VERB"};

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
    {   // MIXDOWN: record the whole song to a WAV
        const bool on = app->mixdown != 0;
        const uint32_t f = on ? (((s_frame / 15) & 1) ? t->pal[0] : t->card) : colourful(t, 0);
        sticker(MIXDOWN_X, TAB_Y, MIXDOWN_W, TAB_H, f, on, 0);
        const float o = on ? 2.0f : 0.0f;
        const u32 c = rgba(ink_on(t, f), 0xFF);
        if (on) {
            const int s = (int)app->mixdown_secs;
            sh_rect(MIXDOWN_X + 7 + o, TAB_Y + 5 + o, 8, 8, c);  // stop square
            ui_text(MIXDOWN_X + 54 + o, TAB_Y + 1 + o, 0.42f, c, C2D_AlignCenter, "%s %d:%02d",
                    app->mixdown == 2 ? "TAIL" : "REC", s / 60, s % 60);
        } else {
            sh_circle(MIXDOWN_X + 11, TAB_Y + 9, 4.5f, rgba(t->pal[0] == f ? t->ink : t->pal[0], 0xFF));
            ui_text(MIXDOWN_X + 54, TAB_Y + 1, 0.42f, c, C2D_AlignCenter, "MIXDOWN");
        }
    }
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
            draw_knob(app, k, i, x + MIX_KNOB_X(i), MIX_KNOB_Y(i), col);
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

// The KEY row (root, scale) and the ARP row (on, mode, rate, octaves).
static void draw_sound_rows(const AppState *app) {
    const Theme *t = ui_theme();
    // KEY: the root, with arrows either side (tap a half to go down / up).
    const uint32_t kf = colourful(t, 3);
    sticker(ROW_X0, KEYROW_Y, ROOT_W, ROW_H, kf, false, 0);
    const u32 kc = rgba(ink_on(t, kf), 0xFF);
    sh_tri(ROW_X0 + 6, KEYROW_Y + ROW_H / 2.0f, 3.5f, 3.14159f, kc);
    sh_tri(ROW_X0 + ROOT_W - 6, KEYROW_Y + ROW_H / 2.0f, 3.5f, 0, kc);
    ui_text(ROW_X0 + ROOT_W / 2.0f, KEYROW_Y + 0.5f, 0.42f, kc, C2D_AlignCenter, "%s", music_root_name(app->key_root));
    const uint32_t sf = colourful(t, 1);
    sticker(SCALE_X, KEYROW_Y, SCALE_W, ROW_H, sf, false, 0);
    const u32 sc = rgba(ink_on(t, sf), 0xFF);
    sh_tri(SCALE_X + 7, KEYROW_Y + ROW_H / 2.0f, 3.5f, 3.14159f, sc);
    sh_tri(SCALE_X + SCALE_W - 7, KEYROW_Y + ROW_H / 2.0f, 3.5f, 0, sc);
    ui_text(SCALE_X + SCALE_W / 2.0f, KEYROW_Y + 0.5f, 0.42f, sc, C2D_AlignCenter, "%s %s", music_root_name(app->key_root),
            music_scale_name(app->key_scale));
    // ARP: on/off, then its settings (dimmed while it's off).
    static const char *const modes[ARP_MODES] = {"UP", "DOWN", "UP-DN", "RAND"};
    static const char *const rates[ARP_RATES] = {"1/8", "1/16", "1/32"};
    char oct[8];
    snprintf(oct, sizeof(oct), "%d OCT", app->arp.octaves);
    const char *labels[4] = {"ARP", modes[app->arp.mode % ARP_MODES], rates[app->arp.rate % ARP_RATES], oct};
    for (int i = 0; i < 4; i++) {
        const float x = ROW_X0 + i * ARP_PITCH;
        const uint32_t f = i == 0 ? (app->arp.on ? t->pal[0] : t->card) : app->arp.on ? colourful(t, 2 + i) : t->card;
        sticker(x, ARPROW_Y, ARP_W, ROW_H, f, i == 0 && app->arp.on, 0);
        const float o = i == 0 && app->arp.on ? 2.0f : 0.0f;
        ui_text(x + ARP_W / 2.0f + o, ARPROW_Y + 0.5f + o, 0.4f, rgba(ink_on(t, f), i == 0 || app->arp.on ? 0xFF : 0x70),
                C2D_AlignCenter, "%s", labels[i]);
    }
}

static void draw_sound_page(const AppState *app) {
    for (int i = 0; i < PARAM_COUNT; i++) draw_slider(app, i);
    draw_sound_rows(app);
    draw_theme_button();
    draw_songs_button();
}

// --------------------------------------------------------- SONGS page ----

static void draw_songs_page(const AppState *app) {
    const Theme *t = ui_theme();
    const u32 ink = rgba(t->ink, 0xFF);
    const bool smp = app->lib_samples;
    if (app->song_count == 0) {
        sh_card(SONG_LIST_X, SONG_LIST_Y0, SONG_LIST_W, SONG_ROWS * SONG_ROW_PITCH - 4, rgba(t->card, 0xFF), ink, 3);
        sh_floppy(SONG_LIST_X + SONG_LIST_W / 2.0f - 14, SONG_LIST_Y0 + 22, 28, rgba(t->pal[1], 0xFF), rgba(t->card, 0xFF), ink);
        ui_text(SONG_LIST_X + SONG_LIST_W / 2.0f, SONG_LIST_Y0 + 60, 0.45f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter,
                app->lib_presets ? "no sounds saved yet" : smp ? "no samples yet" : "no songs yet");
        ui_text(SONG_LIST_X + SONG_LIST_W / 2.0f, SONG_LIST_Y0 + 78, 0.36f, rgba(ink_on(t, t->card), 0xA0), C2D_AlignCenter,
                app->lib_presets ? "SAVE AS keeps this part's sound" : smp ? "SAVE AS keeps the MIC take"
                                                                           : "tap SAVE AS to keep this one");
        if (smp && !app->lib_presets)
            ui_text(SONG_LIST_X + SONG_LIST_W / 2.0f, SONG_LIST_Y0 + 94, 0.32f, rgba(ink_on(t, t->card), 0xA0),
                    C2D_AlignCenter, "or copy WAVs to 3ds/vibe-machine/samples");
    }
    for (int r = 0; r < SONG_ROWS; r++) {
        int i = app->song_scroll + r;
        if (i >= app->song_count) break;
        const SongInfo *s = &app->songs[i];
        const bool sel = i == app->song_sel, cur = !smp && strcmp(s->name, app->song_name) == 0;
        const float y = SONG_LIST_Y0 + r * SONG_ROW_PITCH;
        const uint32_t face = sel ? t->pal[i % THEME_PALETTE] : t->card;
        sticker(SONG_LIST_X, y, SONG_LIST_W, SONG_ROW_H, face, false, 0);
        if (smp)  // a sample: a little waveform
            for (int b = 0; b < 5; b++) {
                float hh = 3.0f + ((b * 7 + i * 3) % 9);
                sh_rect(SONG_LIST_X + 5 + b * 3, y + SONG_ROW_H / 2.0f - hh / 2, 2, hh, rgba(t->pal[(i + 1) % THEME_PALETTE], 0xFF));
            }
        else
            sh_floppy(SONG_LIST_X + 4, y + 4, 14, rgba(t->pal[(i + 1) % THEME_PALETTE], 0xFF), rgba(t->card, 0xFF), ink);
        const uint32_t txt = ink_on(t, face);
        ui_text(SONG_LIST_X + 24, y + 3, 0.42f, rgba(txt, 0xFF), 0, "%s%s", s->name, cur ? " <" : "");
        if (s->bpm >= 0 && !smp)
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
    static const char *const smp_labels[] = {"SAVE AS", NULL, "LOAD", "DELETE", "BACK"};
    const bool has_sel = app->song_sel >= 0 && app->song_sel < app->song_count;
    if (smp)
        ui_text(SONG_BTN_X + SONG_BTN_W / 2.0f, SONG_BTN_Y0 + SONG_BTN_PITCH + 4, 0.4f, rgba(ink_on(t, t->bg_b), 0xFF),
                C2D_AlignCenter, app->lib_presets ? "SOUNDS" : "SAMPLES");
    for (int b = 0; b < 5; b++) {
        const float y = SONG_BTN_Y0 + b * SONG_BTN_PITCH;
        if (smp && !smp_labels[b]) continue;
        bool enabled = b == 0 || b == 4 || (b == 1) || ((b == 2 || b == 3) && has_sel);
        // colourful(): never a black or white face, whatever the theme.
        uint32_t face = !enabled ? t->card : colourful(t, b);
        sticker(SONG_BTN_X, y, SONG_BTN_W, SONG_BTN_H, face, false, 0);
        bool hold = smp ? b == 3 : (b == 3 || b == 4 || (b == 2 && app->song_dirty));
        ui_text(SONG_BTN_X + SONG_BTN_W / 2.0f, y + 2, 0.42f, rgba(ink_on(t, face), enabled ? 0xFF : 0x50), C2D_AlignCenter,
                "%s%s", smp ? smp_labels[b] : labels[b], hold && enabled ? " (hold)" : "");
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
    if (app->clear_hold_t > 0.0f)  // hold: undo
        sh_rect(SEQ_CLEAR_X + 1, TAB_Y + TAB_H - 3, (SEQ_CLEAR_W - 2) * (app->clear_hold_t > 1 ? 1 : app->clear_hold_t), 3,
                rgba(t->pal[0], 0xFF));
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
    const int off = app->bar_page * SEQ_STEPS;  // the bar on screen
    const u32 ink = rgba(t->ink, 0xFF);
    const float top = SEQ_SYNTH_Y - 3, bottom = SEQ_DRUM_Y + SEQ_LANES * SEQ_DRUM_PITCH + 1;

    sh_card(2, top, BOTTOM_W - 4, bottom - top, rgba(t->card, 0xFF), ink, 3);

    // Beat shading (every other group of four) and the playhead column.
    for (int s = 0; s < SEQ_STEPS; s++) {
        float x = SEQ_GRID_X + s * SEQ_CELL_W;
        if ((s / 4) & 1) sh_rect(x, top + 2, SEQ_CELL_W, bottom - top - 4, rgba(t->ink, 0x12));
        if (st->playing && off + s == st->step) sh_rect(x, top + 2, SEQ_CELL_W, bottom - top - 4, rgba(t->pal[0], 0x55));
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

    for (int c = 0; c < SEQ_STEPS; c++) {
        const int s = off + c;  // the pattern's step in this column
        const float x = SEQ_GRID_X + c * SEQ_CELL_W;
        const bool head = st->playing && s == st->step;
        const float pop = head ? -2.0f : 0.0f;  // cells under the playhead lift off

        // Accent row: a small triangle that stands up on accented steps.
        if ((p->accents >> s) & 1ull)
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
    if (app->song_btn_hold > 0)  // hold: open / close the SONG sheet
        sh_rect(SEQ_SONG_X + 1, SEQ_SLOT_Y + SEQ_SLOT_H - 3, (SEQ_SONG_W - 2) * app->song_btn_hold / 30.0f, 3,
                rgba(ink_on(t, st->song ? t->pal[3] : t->card), 0xFF));
    sticker(SEQ_ROLL_X, SEQ_SLOT_Y, SEQ_ROLL_W, SEQ_SLOT_H, app->roll_view ? t->pal[1] : t->card, !app->roll_view, "ROLL");

    {   // BAR: which bar is on screen, of how many (tap: next; hold: length)
        const int bars = seq_bars(&app->pattern), playing_bar = st->playing ? st->step / SEQ_STEPS : -1;
        const uint32_t f = bars > 1 ? colourful(t, 2) : t->card;
        sticker(SEQ_BAR_X, SEQ_SLOT_Y, SEQ_BAR_W, SEQ_SLOT_H, f, false, 0);
        const u32 c = rgba(ink_on(t, f), 0xFF);
        ui_text(SEQ_BAR_X + SEQ_BAR_W / 2.0f, SEQ_SLOT_Y + 0.5f, 0.38f, c, C2D_AlignCenter, "%d/%d", app->bar_page + 1, bars);
        for (int b = 0; b < bars; b++)  // one dot a bar; the playing one lit
            sh_rect(SEQ_BAR_X + SEQ_BAR_W / 2.0f - bars * 2.5f + b * 5 + 0.5f, SEQ_SLOT_Y + SEQ_SLOT_H - 4, 3, 2,
                    rgba(b == playing_bar ? t->pal[0] : t->ink, b == playing_bar ? 0xFF : 0x60));
        if (app->bar_hold > 0)
            sh_rect(SEQ_BAR_X + 1, SEQ_SLOT_Y + SEQ_SLOT_H - 2, (SEQ_BAR_W - 2) * app->bar_hold / 30.0f, 2, c);
    }

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
    const int off = app->bar_page * SEQ_STEPS;  // the bar on screen
    for (int s2 = 0; s2 < SEQ_STEPS; s2++) {
        float x = gx + s2 * ROLL_CELL_W;
        if ((s2 / 4) & 1) sh_rect(x, gy, ROLL_CELL_W, gh, rgba(t->ink, 0x0E));
        sh_rect(x, gy, 1, gh, rgba(t->ink, s2 % 4 == 0 ? 0x40 : 0x14));
        if (st->playing && off + s2 == st->step) sh_rect(x, gy, ROLL_CELL_W, gh, rgba(t->pal[0], 0x40));
    }
    // Other parts' notes, faintly, to write against.
    for (int i = 0; i < p->count; i++) {
        const SeqRollNote *n = &p->notes[i];
        if (n->part == app->part) continue;
        int r = music_roll_row(n->pitch, chrom) - app->roll_scroll;
        if (r < 0 || r >= ROLL_ROWS) continue;
        if (n->step + n->len <= off || n->step >= off + SEQ_STEPS) continue;  // not in this bar
        float x = gx + (n->step - off) * ROLL_CELL_W + 1, y = gy + (ROLL_ROWS - 1 - r) * ROLL_ROW_H;
        float w = n->len * ROLL_CELL_W - 2;
        if (n->flags & SEQ_NOTE_HALF) x += ROLL_CELL_W / 2.0f, w -= ROLL_CELL_W / 2.0f;  // an off-beat 1/32
        if (x < gx) {
            w -= gx - x;
            x = gx;
        }
        if (x + w > gx + gw) w = gx + gw - x;
        sh_rect(x, y + 2, w, ROLL_ROW_H - 5, rgba(part_rgb(t, n->part), 0x55));
    }
    // This part's notes.
    for (int i = 0; i < p->count; i++) {
        const SeqRollNote *n = &p->notes[i];
        if (n->part != app->part) continue;
        int r = music_roll_row(n->pitch, chrom) - app->roll_scroll;
        if (r < 0 || r >= ROLL_ROWS) continue;
        if (n->step + n->len <= off || n->step >= off + SEQ_STEPS) continue;  // not in this bar
        float x = gx + (n->step - off) * ROLL_CELL_W + 1, y = gy + (ROLL_ROWS - 1 - r) * ROLL_ROW_H;
        float w = n->len * ROLL_CELL_W - 2;
        if (n->flags & SEQ_NOTE_HALF) x += ROLL_CELL_W / 2.0f, w -= ROLL_CELL_W / 2.0f;  // an off-beat 1/32
        if (x < gx) {  // started in an earlier bar
            w -= gx - x;
            x = gx;
        }
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
        if (n->vel < 120)  // soft: washed out
            sh_rect(x + lift + 2, y + lift + 2, w - 4, ROLL_ROW_H - 5, rgba(t->card, 0x90));
        else if (n->vel > 200)  // loud: a bar along the top
            sh_rect(x + lift + 2, y + lift + 1.5f, w - 4, 1.5f, rgba(ink_on(t, face), 0xFF));
        if (n->flags & SEQ_NOTE_SLIDE)  // slides into the next note: a ramp off its end
            sh_quad(x + w - 1 + lift, y + ROLL_ROW_H - 2 + lift, x + w + 5 + lift, y + lift, x + w + 7 + lift, y + lift,
                    x + w + 1 + lift, y + ROLL_ROW_H - 2 + lift, ink);
    }
    // Scroll hints at the strip's ends.
    if (app->roll_scroll + ROLL_ROWS < rows) sh_tri(ROLL_KEYS_X + ROLL_KEYS_W / 2.0f, gy - 2, 3, -1.5708f, rgba(t->pal[2], 0xFF));
    if (app->roll_scroll > 0) sh_tri(ROLL_KEYS_X + ROLL_KEYS_W / 2.0f, gy + gh + 1, 3, 1.5708f, rgba(t->pal[2], 0xFF));
}

// The SONG sheet: the arrangement as stickers, the playing one filling up.
static void draw_arr(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqArrangement *a = &app->arr;
    const u32 ink = rgba(t->ink, 0xFF);
    const bool sel_ok = app->arr_sel >= 0 && app->arr_sel < a->count;
    // Header: END, length, the picked row's mutes and DEL.
    sticker(ARR_END_X, ARR_HEAD_Y, ARR_END_W, ARR_HEAD_H, a->loop ? t->pal[1] : t->pal[0], false,
            a->loop ? "LOOP" : "STOP");
    int bars = 0;
    for (int i = 0; i < a->count; i++) bars += a->rows[i].reps * app->seq.slot_bars[a->rows[i].slot];
    const int secs = (int)(bars * 240.0f / (app->seq.bpm > 0 ? app->seq.bpm : 120) + 0.5f);
    ui_text(ARR_END_X + ARR_END_W + 6, ARR_HEAD_Y + 1, 0.36f, rgba(ink_on(t, t->bg_b), 0xFF), 0, "%d BARS %d:%02d", bars,
            secs / 60, secs % 60);
    if (sel_ok) {
        const uint8_t m = a->rows[app->arr_sel].mutes;
        for (int k = 0; k <= PARTS; k++) {
            const bool off = (m >> k) & 1u;
            const uint32_t f = off ? t->card : part_rgb(t, k);
            char lab[2] = {k < PARTS ? (char)('1' + k) : 'D', 0};
            const float mx = ARR_MUTE_X0 + k * ARR_MUTE_PITCH;
            sticker(mx, ARR_HEAD_Y, ARR_MUTE_W, ARR_HEAD_H, f, off, lab);
            if (off) sh_rect(mx + 5, ARR_HEAD_Y + ARR_HEAD_H / 2.0f + 2, ARR_MUTE_W - 10, 1.5f, ink);  // struck out
        }
        sticker(ARR_DEL_X, ARR_HEAD_Y, ARR_DEL_W, ARR_HEAD_H, t->card, false, "DEL");
    } else if (a->count == 0) {
        ui_text(ARR_MUTE_X0 + 70, ARR_HEAD_Y + 1, 0.34f, rgba(ink_on(t, t->bg_b), 0xC0), C2D_AlignCenter,
                "tap + to add a row");
    }
    // The rows.
    for (int i = 0; i <= a->count && i < SEQ_ARR_ROWS; i++) {
        const float x = ARR_X0 + (i % ARR_COLS) * ARR_PITCH_X, y = ARR_Y0 + (i / ARR_COLS) * ARR_PITCH_Y;
        if (i == a->count) {  // the next free sticker: add a row
            sh_card(x, y, ARR_CELL_W, ARR_CELL_H, rgba(t->card, 0x90), rgba(t->ink, 0x80), 0);
            ui_text(x + ARR_CELL_W / 2.0f, y + 1, 0.55f, rgba(ink_on(t, t->card), 0xC0), C2D_AlignCenter, "+");
            break;
        }
        const SeqArrRow *r = &a->rows[i];
        const uint32_t face = t->pal[r->slot % THEME_PALETTE];
        const bool sel = i == app->arr_sel, playing = i == app->seq.arr_row;
        sticker(x, y, ARR_CELL_W, ARR_CELL_H, face, false, 0);
        if (playing) {  // fills as the row plays through its repeats
            const float f = (app->seq.arr_rep + (app->seq.step + 1) / (float)seq_len(&app->pattern)) / r->reps;
            sh_rect(x + 1, y + ARR_CELL_H - 4, (ARR_CELL_W - 2) * (f > 1 ? 1 : f), 3, rgba(ink_on(t, face), 0xFF));
        }
        const u32 txt = rgba(ink_on(t, face), 0xFF);
        ui_text(x + 4, y + 1, 0.5f, txt, 0, "%c", 'A' + r->slot);
        if (r->reps > 1) ui_text(x + ARR_CELL_W - 3, y + 3, 0.36f, txt, C2D_AlignRight, "x%d", r->reps);
        for (int k = 0; k <= PARTS; k++)  // tiny dots: the parts dropped out of this row
            if ((r->mutes >> k) & 1u) sh_rect(x + 4 + k * 5, y + ARR_CELL_H - 8, 3, 3, rgba(t->ink, 0xC0));
        if (sel) {  // the picked row: a thick outline and a corner triangle
            const float ox = x - 3, oy = y - 3, ow = ARR_CELL_W + 6, oh = ARR_CELL_H + 6;
            sh_rect(ox, oy, ow, 2, ink);
            sh_rect(ox, oy + oh - 2, ow, 2, ink);
            sh_rect(ox, oy, 2, oh, ink);
            sh_rect(ox + ow - 2, oy, 2, oh, ink);
            sh_tri(x + ARR_CELL_W - 4, y + 4, 4, 1.5708f, ink);
        }
    }
}

static void draw_seq_page(const AppState *app) {
    draw_transport(app);
    if (app->arr_view)
        draw_arr(app);
    else if (app->roll_view)
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
    const int page = app->syn_page;
    for (int slot = 0; slot < SOUNDS_PER_PAGE; slot++) {
        const int i = page * SOUNDS_PER_PAGE + slot;
        if (i >= sound_count()) break;
        const bool on = app->sound == i;
        const float x = SYN_TILE_X0 + (slot % SYN_TILE_COLS) * SYN_TILE_PITCH_X;
        const float y = SYN_TILE_Y0 + (slot / SYN_TILE_COLS) * SYN_TILE_PITCH_Y;
        const uint32_t face = on ? t->pal[i % THEME_PALETTE] : t->card;
        sh_card(x, y, SYN_TILE_W, SYN_TILE_H, rgba(face, 0xFF), rgba(t->ink, 0xFF), on ? 3.0f : 1.5f);
        uint32_t body = on ? t->card : t->pal[i % THEME_PALETTE];
        float bob = on ? sinf(s_frame * 0.15f) * 1.2f : 0.0f;
        draw_mascot(i, x + 10, y + SYN_TILE_H / 2.0f + bob, on ? 6.5f : 5.5f, 0, 0, body);
        ui_text(x + 32, y + 1.5f, 0.34f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter, "%s", sound_get(i)->tag);
    }
    // The PAGE tile: which page, and an arrow to the next. A dot if the
    // sound playing is on the other page.
    {
        const float x = SYN_TILE_X0 + (SYN_PAGE_SLOT % SYN_TILE_COLS) * SYN_TILE_PITCH_X;
        const float y = SYN_TILE_Y0 + (SYN_PAGE_SLOT / SYN_TILE_COLS) * SYN_TILE_PITCH_Y;
        const uint32_t f = colourful(t, 1);
        sticker(x, y, SYN_TILE_W, SYN_TILE_H, f, false, 0);
        const u32 c = rgba(ink_on(t, f), 0xFF);
        ui_text(x + 20, y + 1.5f, 0.34f, c, C2D_AlignCenter, "%d/%d", page + 1, ui_sound_pages());
        sh_tri(x + SYN_TILE_W - 9, y + SYN_TILE_H / 2.0f, 4.5f, 0.0f, c);
        if (app->sound / SOUNDS_PER_PAGE != page) sh_rect(x + 3, y + 3, 3, 3, c);
    }
    // EDIT | KIT | PRESET along the bottom.
    const PartEdit *pe = &app->parts[app->part].pe;
    const uint32_t ef = app->edit_view ? t->pal[0] : colourful(t, 2);
    sticker(SYN_TILE_X0, SYN_EDIT_Y, SYN_EDIT_W, SYN_TILE_H, ef, app->edit_view, 0);
    const float o = app->edit_view ? 2.0f : 0.0f;
    ui_text(SYN_TILE_X0 + SYN_EDIT_W / 2.0f + o, SYN_EDIT_Y + 2 + o, 0.32f, rgba(ink_on(t, ef), 0xFF), C2D_AlignCenter,
            pe->cur.set ? "EDIT*" : "EDIT");
    const uint32_t kf = app->kit_view ? t->pal[0] : colourful(t, 3);
    sticker(SYN_KIT_X, SYN_EDIT_Y, SYN_EDIT_W, SYN_TILE_H, kf, app->kit_view, 0);
    const float ko = app->kit_view ? 2.0f : 0.0f;
    ui_text(SYN_KIT_X + SYN_EDIT_W / 2.0f + ko, SYN_EDIT_Y + 2 + ko, 0.32f, rgba(ink_on(t, kf), 0xFF), C2D_AlignCenter,
            app->kit.edited ? "KIT*" : "KIT");
    const uint32_t pf = colourful(t, 4);
    sticker(SYN_PRESET_X, SYN_EDIT_Y, SYN_PRESET_W, SYN_TILE_H, pf, false, 0);
    sh_floppy(SYN_PRESET_X + SYN_PRESET_W / 2.0f - 6, SYN_EDIT_Y + 2.5f, 12, rgba(t->pal[1], 0xFF), rgba(t->card, 0xFF),
              rgba(t->ink, 0xFF));
}

// ------------------------------------------------------------- KIT ----

static void kit_arrow(float x, float y, float h, bool left, uint32_t col) {
    const Theme *t = ui_theme();
    sticker(x, y, KIT_ARROW_W, h, col, false, 0);
    sh_tri(x + KIT_ARROW_W / 2.0f, y + h / 2.0f, 4.5f, left ? 3.14159f : 0.0f, rgba(ink_on(t, col), 0xFF));
}

static void draw_kit_view(const AppState *app) {
    const Theme *t = ui_theme();
    const DrumKit *k = &app->kit;
    // The kit.
    kit_arrow(KIT_PREV_X, KIT_HEAD_Y, KIT_HEAD_H, true, colourful(t, 1));
    kit_arrow(KIT_NEXT_X, KIT_HEAD_Y, KIT_HEAD_H, false, colourful(t, 1));
    const uint32_t nf = colourful(t, 4);
    sh_card(KIT_NAME_X, KIT_HEAD_Y, KIT_NAME_W, KIT_HEAD_H, rgba(nf, 0xFF), rgba(t->ink, 0xFF), 1.5f);
    ui_text(KIT_NAME_X + KIT_NAME_W / 2.0f, KIT_HEAD_Y + 1, 0.4f, rgba(ink_on(t, nf), 0xFF), C2D_AlignCenter, "%s%s",
            drum_kit_name(k->kit), k->edited ? " (YOURS)" : "");
    // The lanes: the picked one stands out; a lane being hit flashes.
    for (int l = 0; l < DRUM_LANES; l++) {
        const float x = KIT_LANE_X0 + (l % 2) * KIT_LANE_PITCH_X, y = KIT_LANE_Y0 + (l / 2) * KIT_LANE_PITCH_Y;
        const bool on = app->kit_lane == l, hit = app->drum_flash[l] > 0;
        const uint32_t face = hit ? t->pal[0] : on ? part_rgb(t, l % PARTS) : t->card;
        sh_card(x, y, KIT_LANE_W, KIT_LANE_H, rgba(face, 0xFF), rgba(t->ink, 0xFF), on ? 2.5f : 1.0f);
        const u32 c = rgba(ink_on(t, face), 0xFF);
        ui_text(x + 4, y + 1, 0.32f, c, 0, "%s", drum_lane_name(l));
        ui_text(x + KIT_LANE_W - 4, y + 1, 0.3f, rgba(ink_on(t, face), 0xB0), C2D_AlignRight, "%.11s",
                drum_lane_sample_name(k, l));
        if ((k->edited >> l) & 1u) sh_rect(x + 2, y + KIT_LANE_H - 3, 4, 2, c);  // changed from the stock kit
    }
    // The picked lane: its sample, MIC, TUNE and LEVEL.
    const int l = app->kit_lane;
    kit_arrow(KIT_PREV_X, KIT_SMP_Y, KIT_SMP_H, true, colourful(t, 2));
    kit_arrow(KIT_SNEXT_X, KIT_SMP_Y, KIT_SMP_H, false, colourful(t, 2));
    sh_card(KIT_SNAME_X, KIT_SMP_Y, KIT_SNAME_W, KIT_SMP_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 1.0f);
    const u32 ic = rgba(ink_on(t, t->card), 0xFF);
    ui_text(KIT_SNAME_X + 3, KIT_SMP_Y + 0.5f, 0.3f, ic, 0, "%s", drum_lane_sample_name(k, l));
    ui_text(KIT_SNAME_X + KIT_SNAME_W - 3, KIT_SMP_Y + 0.5f, 0.26f, rgba(ink_on(t, t->card), 0x90), C2D_AlignRight, "%.10s",
            drum_lane_bank_name(k, l));
    sticker(KIT_MIC_X, KIT_SMP_Y, KIT_MIC_W, KIT_SMP_H, colourful(t, 0), false, 0);
    ui_text(KIT_MIC_X + KIT_MIC_W / 2.0f, KIT_SMP_Y + 0.5f, 0.32f, rgba(ink_on(t, colourful(t, 0)), 0xFF), C2D_AlignCenter,
            "MIC");
    for (int s = 0; s < 2; s++) {
        const float x = s ? KIT_LEVEL_X : KIT_TUNE_X;
        const float v = s ? k->lane[l].level / (float)DRUM_LEVEL_MAX : (k->lane[l].tune + DRUM_TUNE_MAX) / (2.0f * DRUM_TUNE_MAX);
        const uint32_t col = colourful(t, 1 + s);
        sh_card(x, KIT_SLIDER_Y, KIT_SLIDER_W, KIT_SLIDER_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF),
                app->kit_grab == 1 + s ? 0 : 1.5f);
        if (s) {
            sh_rect(x + 1.5f, KIT_SLIDER_Y + 1.5f, (KIT_SLIDER_W - 3) * v, KIT_SLIDER_H - 3, rgba(col, 0xFF));
        } else {  // TUNE fills from the middle
            const float mid = x + KIT_SLIDER_W / 2.0f, end = x + 1.5f + (KIT_SLIDER_W - 3) * v;
            sh_rect(mid < end ? mid : end, KIT_SLIDER_Y + 1.5f, fabsf(end - mid), KIT_SLIDER_H - 3, rgba(col, 0xFF));
            sh_rect(mid - 0.5f, KIT_SLIDER_Y + 1, 1, KIT_SLIDER_H - 2, rgba(t->ink, 0x80));
        }
        ui_text(x + 4, KIT_SLIDER_Y - 0.5f, 0.3f, ic, 0, s ? "LEVEL" : "TUNE");
        if (s)
            ui_text(x + KIT_SLIDER_W - 4, KIT_SLIDER_Y - 0.5f, 0.3f, ic, C2D_AlignRight, "%d%%", k->lane[l].level);
        else
            ui_text(x + KIT_SLIDER_W - 4, KIT_SLIDER_Y - 0.5f, 0.3f, ic, C2D_AlignRight, "%+d", k->lane[l].tune);
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

// The slice (0..slices-1) any live note is playing, as a bit mask; with
// CHOP off, bit 0 means "the region is sounding".
static unsigned live_slices(const AppState *app) {
    unsigned m = 0;
    for (int p = 0; p < 128; p++)
        if ((app->live_pitch[p >> 3] >> (p & 7)) & 1u)
            m |= 1u << (app->sample_edit.slices ? sampler_slice_of_pitch(p, app->sample_edit.slices) : 0);
    return m;
}

// MIC: the sample editor. The take, START..END bright and the rest dimmed,
// slice lines when chopped, and handles to drag.
static void draw_sampler(const AppState *app, const Sound *s) {
    const Theme *t = ui_theme();
    const u32 ink = rgba(t->ink, 0xFF);
    const uint32_t lab = ink_on(t, t->card);
    const SampleEdit *e = &app->sample_edit;
    if (e->slices) {  // chopped: the slices can go onto the drum lanes
        sticker(SMP_TOKIT_X, SYN_DICE_Y, SMP_TOKIT_W, 16, colourful(t, 3), false, 0);
        sh_tri(SMP_TOKIT_X + 8, SYN_DICE_Y + 8, 4, 0.0f, rgba(ink_on(t, colourful(t, 3)), 0xFF));
        ui_text(SMP_TOKIT_X + 27, SYN_DICE_Y, 0.36f, rgba(ink_on(t, colourful(t, 3)), 0xFF), C2D_AlignCenter, "KIT");
    } else {
        ui_text(SYN_PAD_X + 6, SYN_PAD_Y + 2, 0.4f, rgba(lab, 0xFF), 0, "%s", s->name);
    }

    // CHOP, LIB, REC.
    const uint32_t cf = e->slices ? colourful(t, 2) : t->card;
    sticker(SMP_CHOP_X, SYN_DICE_Y, SMP_CHOP_W, 16, cf, false, 0);
    ui_text(SMP_CHOP_X + SMP_CHOP_W / 2.0f, SYN_DICE_Y, 0.36f, rgba(ink_on(t, cf), 0xFF), C2D_AlignCenter,
            e->slices ? "CHOP %d" : "CHOP", e->slices);
    sticker(SMP_LIB_X, SYN_DICE_Y, SMP_LIB_W, 16, colourful(t, 4), false, 0);
    sh_floppy(SMP_LIB_X + 3, SYN_DICE_Y + 3, 10, rgba(t->pal[1], 0xFF), rgba(t->card, 0xFF), ink);
    ui_text(SMP_LIB_X + 28, SYN_DICE_Y, 0.36f, rgba(ink_on(t, colourful(t, 4)), 0xFF), C2D_AlignCenter, "LIB");
    uint32_t face = app->mic_rec ? (((s_frame / 8) & 1) ? t->pal[0] : t->card) : t->card;
    sticker(SYN_REC_X, SYN_DICE_Y, SYN_REC_W, 16, face, app->mic_rec, 0);
    float o = app->mic_rec ? 2.0f : 0.0f;
    sh_circle(SYN_REC_X + 8 + o, SYN_DICE_Y + 8 + o, 4.0f, rgba(app->mic_ok ? t->pal[0] : t->ink, 0xFF));
    ui_text(SYN_REC_X + 24 + o, SYN_DICE_Y + o, 0.36f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter,
            app->mic_ok ? "REC" : "N/A");

    // The take.
    const float x0 = SMP_WAVE_X, y0 = SMP_WAVE_Y, w = SMP_WAVE_W, h = SMP_WAVE_H, mid = y0 + h / 2.0f;
    sh_card(x0 - 2, y0 - 2, w + 4, h + 4, rgba(t->bg_a, 0xFF), ink, 0);
    const float sx = x0 + e->start * w, ex = x0 + e->end * w;
    const unsigned lit = live_slices(app);
    const int n = e->slices ? e->slices : 1;
    for (int k = 0; k < n; k++) {  // each slice gets a tint; lit while it plays
        const float a = x0 + sampler_bound(e, k) * w, b = x0 + sampler_bound(e, k + 1) * w;
        const bool on = ((lit >> k) & 1u) || (app->smp_flash_frames > 0 && (app->smp_flash == k || app->smp_flash < 0));
        sh_rect(a, y0, b - a, h, rgba(e->slices ? t->pal[k % THEME_PALETTE] : t->pal[2], on ? 0x90 : (k & 1) ? 0x30 : 0x48));
        if (e->slices && k > 0) sh_rect(a - 0.5f, y0, 1, h, rgba(t->ink, 0xA0));
        if (e->slices) ui_text((a + b) / 2.0f, y0 + 1, 0.3f, rgba(t->ink, 0xC0), C2D_AlignCenter, "%d", k + 1);
    }
    const float bw = w / SAMPLER_OVERVIEW;
    for (int i = 0; i < SAMPLER_OVERVIEW; i++) {
        const float a = app->take_overview[i] * (h / 2.0f - 3.0f), bx = x0 + i * bw;
        const bool in = bx + bw > sx && bx < ex;
        sh_rect(bx, mid - a, bw * 0.75f, 2 * a + 1, rgba(t->ink, in ? 0xE0 : 0x40));
    }
    // CHOP's cuts: a grip each under the take (drag to move a cut).
    for (int k = 0; k + 1 < e->slices; k++) {
        const float cx = x0 + e->cut[k] * w;
        const bool grabbed = app->smp_grab == SMP_GRAB_CUT + k;
        const uint32_t c = t->pal[(k + 1) % THEME_PALETTE];
        if (grabbed) sh_rect(cx - 1, y0 - 2, 2, h + 4, rgba(t->ink, 0xFF));
        const float gw = grabbed ? 11 : 8;
        sh_card(cx - gw / 2, y0 + h + 2, gw, SMP_GRIP_H, rgba(c, 0xFF), ink, grabbed ? 0 : 1.5f);
    }
    // Handles: a line through the take and a grip underneath.
    for (int hnd = 0; hnd < 2; hnd++) {
        const float hx = hnd ? ex : sx;
        const bool grabbed = app->smp_grab == 1 + hnd;
        const uint32_t c = hnd ? t->pal[0] : t->pal[3];
        sh_rect(hx - 1, y0 - 2, 2, h + 4, rgba(c, 0xFF));
        const float gw = grabbed ? 18 : 15, gx = hnd ? hx - gw : hx;  // grips point inwards
        sh_card(gx, y0 + h + 2, gw, SMP_GRIP_H, rgba(c, 0xFF), ink, grabbed ? 0 : 1.5f);
        ui_text(gx + gw / 2.0f, y0 + h + 1.5f, 0.32f, rgba(ink_on(t, c), 0xFF), C2D_AlignCenter, hnd ? "E" : "S");
    }
    // DECAY and TONE.
    for (int m = 0; m < 2; m++) {
        const float bx = SMP_SLIDER_X0 + m * SMP_SLIDER_PITCH, v = app->macro[m];
        const uint32_t col = colourful(t, m);
        sh_card(bx, SMP_SLIDER_Y, SMP_SLIDER_W, SMP_SLIDER_H, rgba(t->card, 0xFF), ink, app->smp_grab == 3 + m ? 0 : 2);
        sh_rect(bx + 1.5f, SMP_SLIDER_Y + 1.5f, (SMP_SLIDER_W - 3) * v, SMP_SLIDER_H - 3, rgba(col, 0xFF));
        ui_text(bx + 4, SMP_SLIDER_Y + 0.5f, 0.34f, rgba(ink_on(t, v > 0.35f ? col : t->card), 0xFF), 0, "%s",
                s->macro_name[m]);
        ui_text(bx + SMP_SLIDER_W - 4, SMP_SLIDER_Y + 0.5f, 0.34f, rgba(lab, 0xFF), C2D_AlignRight, "%d",
                (int)(v * 100.0f + 0.5f));
    }
}

static void draw_creature_pad(const AppState *app, const Sound *s) {
    if (app->sound == sound_mic_index()) {
        draw_sampler(app, s);
        return;
    }
    const Theme *t = ui_theme();
    const u32 ink = rgba(t->ink, 0xFF);
    const float x0 = SYN_PAD_X + 8, y0 = SYN_PAD_Y + 20, w = SYN_PAD_W - 16, h = SYN_PAD_H - 28;
    sh_grid(x0, y0, w, h, 13, rgba(t->ink, 0x16));
    ui_text(SYN_PAD_X + 6, SYN_PAD_Y + 2, 0.4f, rgba(ink_on(t, t->card), 0xFF), 0, "%s", s->name);
    if (app->sound == sound_sample_index()) {  // SAMPLE: where its sample comes from, and how long it is
        static const char *const src[2] = {"MIC", "LIB"};
        for (int i = 0; i < 2; i++) {
            const float cx = DRAW_CHIP_X + i * DRAW_CHIP_PITCH;
            sh_card(cx, DRAW_CHIP_Y, DRAW_CHIP_W, DRAW_CHIP_H, rgba(colourful(t, i), 0xFF), rgba(t->ink, 0xFF), 1.5f);
            ui_text(cx + DRAW_CHIP_W / 2.0f, DRAW_CHIP_Y + 0.5f, 0.3f, rgba(ink_on(t, colourful(t, i)), 0xFF),
                    C2D_AlignCenter, "%s", src[i]);
        }
        const float secs = app->part_sample_secs[app->part];
        if (secs > 0.0f)
            ui_text(SYN_DICE_X - 4, SYN_PAD_Y + 3, 0.32f, rgba(ink_on(t, t->card), 0x90), C2D_AlignRight, "%.1fs", secs);
        else
            ui_text(SYN_DICE_X - 4, SYN_PAD_Y + 3, 0.32f, rgba(ink_on(t, t->card), 0x90), C2D_AlignRight, "boing");
    } else if (app->sound != sound_mic_index()) {
        ui_text(SYN_DICE_X - 4, SYN_PAD_Y + 3, 0.32f, rgba(ink_on(t, t->card), 0x90), C2D_AlignRight, "%s", s->blurb);
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

// DRAW: the wave you drew, as you hear it (smoothed, centred, levelled),
// over the raw points; drag across to redraw it.
static void draw_drawpad(const AppState *app, const Sound *s) {
    const Theme *t = ui_theme();
    const float x0 = SYN_PAD_X + 8, y0 = SYN_PAD_Y + 20, w = SYN_PAD_W - 16, h = SYN_PAD_H - 28;
    sh_grid(x0, y0, w, h, 13, rgba(t->ink, 0x16));
    sh_rect(x0, y0 + h / 2.0f - 0.5f, w, 1, rgba(t->ink, 0x50));  // zero
    ui_text(SYN_PAD_X + 6, SYN_PAD_Y + 2, 0.4f, rgba(ink_on(t, t->card), 0xFF), 0, "%s", s->name);
    static const char *const shapes[4] = {"SIN", "SAW", "SQR", "TRI"};
    for (int i = 0; i < 4; i++) {
        const float cx = DRAW_CHIP_X + i * DRAW_CHIP_PITCH;
        sh_card(cx, DRAW_CHIP_Y, DRAW_CHIP_W, DRAW_CHIP_H, rgba(colourful(t, i), 0xFF), rgba(t->ink, 0xFF), 1.5f);
        ui_text(cx + DRAW_CHIP_W / 2.0f, DRAW_CHIP_Y + 0.5f, 0.3f, rgba(ink_on(t, colourful(t, i)), 0xFF), C2D_AlignCenter, "%s",
                shapes[i]);
    }
    const DrawWave *wv = &app->parts[app->part].wave;
    for (int i = 0; i < DRAW_POINTS; i++) {  // the points you drew
        const float px = x0 + w * i / (DRAW_POINTS - 1.0f), py = y0 + h * (0.5f - wv->pt[i] / 254.0f);
        sh_rect(px - 1, py - 1, 2, 2, rgba(t->ink, 0x50));
    }
    static int16_t cyc[DRAW_CYCLE];
    draw_render(wv, cyc);
    const uint32_t col = part_rgb(t, app->part);
    float lx = x0, ly = y0 + h * (0.5f - cyc[0] / 65536.0f);
    for (int i = 2; i <= DRAW_CYCLE; i += 2) {  // what plays
        const float nx = x0 + w * i / (float)DRAW_CYCLE, ny = y0 + h * (0.5f - cyc[i % DRAW_CYCLE] / 65536.0f);
        const float dx = nx - lx, dy = ny - ly;
        sh_bar((lx + nx) / 2, (ly + ny) / 2, sqrtf(dx * dx + dy * dy) + 1.0f, 3, atan2f(dy, dx), rgba(col, 0xFF));
        lx = nx;
        ly = ny;
    }
    if (!app->pad_touch)
        ui_text(x0 + w / 2.0f, y0 + h - 12, 0.34f, rgba(ink_on(t, t->card), 0x90), C2D_AlignCenter, "draw a wave with the stylus");
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

// ------------------------------------------------------------ EDIT ----

// A horizontal slider in a sticker: label on the left, value on the right.
static void edit_slider(float x, float y, float w, float v, const char *label, const char *value, bool set, bool bipolar,
                        uint32_t col) {
    const Theme *t = ui_theme();
    sh_card(x, y, w, ED_ROW_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 1.5f);
    const float ix = x + 2, iw = w - 4;
    if (bipolar) {  // fills from the middle
        const float mid = ix + iw / 2.0f, end = ix + iw * v;
        sh_rect(mid < end ? mid : end, y + 2, fabsf(end - mid), ED_ROW_H - 4, rgba(col, set ? 0xFF : 0x60));
        sh_rect(mid - 0.5f, y + 2, 1, ED_ROW_H - 4, rgba(t->ink, 0x80));
    } else {
        sh_rect(ix, y + 2, iw * v, ED_ROW_H - 4, rgba(col, set ? 0xFF : 0x60));
    }
    const u32 c = rgba(ink_on(t, t->card), 0xFF);
    ui_text(x + 4, y + 0.5f, 0.32f, c, 0, "%s", label);
    ui_text(x + w - 4, y + 0.5f, 0.32f, c, C2D_AlignRight, "%s", value);
}

static void edit_chip(float x, float y, float w, const char *label, bool on, uint32_t col) {
    const Theme *t = ui_theme();
    sticker(x, y, w, ED_ROW_H, on ? col : t->card, on, 0);
    const float o = on ? 2.0f : 0.0f;
    ui_text(x + w / 2.0f + o, y + 0.5f + o, 0.32f, rgba(ink_on(t, on ? col : t->card), 0xFF), C2D_AlignCenter, "%s", label);
}

// An XY pad with a ball, the axis names and values.
static void edit_xy(const SynthEdit *e, EditParam px, EditParam py, uint32_t col) {
    const Theme *t = ui_theme();
    const float x0 = ED_BODY_X, y0 = ED_BODY_Y, w = ED_BODY_W, h = ED_PAD_H;
    sh_card(x0, y0, w, h, rgba(t->bg_a, 0xFF), rgba(t->ink, 0xFF), 0);
    sh_grid(x0 + 2, y0 + 2, w - 4, h - 4, 12, rgba(t->ink, 0x18));
    const bool set = ((e->set >> px) & 1u) || ((e->set >> py) & 1u);
    const float bx = x0 + 6 + e->p[px] * (w - 12), by = y0 + 6 + (1.0f - e->p[py]) * (h - 12);
    for (float yy = by + 5; yy < y0 + h - 2; yy += 5) sh_rect(bx - 0.5f, yy, 1, 2.5f, rgba(col, 0xA0));
    sh_circle(bx + 1.5f, by + 1.5f, 6, rgba(t->ink, 0xFF));
    sh_circle(bx, by, 6, rgba(col, set ? 0xFF : 0x80));
    char a[16], b[16];
    edit_label(px, e->p[px], a, sizeof(a));
    edit_label(py, e->p[py], b, sizeof(b));
    const u32 c = rgba(ink_on(t, t->bg_a), 0xFF);
    ui_text(x0 + w - 4, y0 + h - 13, 0.32f, c, C2D_AlignRight, "%s %s", edit_param_name(px), a);
    ui_text(x0 + 4, y0 + 2, 0.32f, c, 0, "%s %s", edit_param_name(py), b);
}

static void draw_edit_view(const AppState *app) {
    const Theme *t = ui_theme();
    const PartEdit *pe = &app->parts[app->part].pe;
    const SynthEdit *e = &pe->cur;
    static const char *const tabs[EDP_PAGES] = {"ENV", "FILT", "LFO", "VOICE"};
    for (int i = 0; i < EDP_PAGES; i++) {
        const bool on = app->edit_page == i;
        const uint32_t f = on ? t->pal[(i + 1) % THEME_PALETTE] : t->card;
        sticker(ED_TAB_X0 + i * ED_TAB_PITCH, ED_TAB_Y, ED_TAB_W, ED_TAB_H, f, !on, tabs[i]);
    }
    sticker(ED_RESET_X, ED_TAB_Y, ED_RESET_W, ED_TAB_H, e->set ? colourful(t, 0) : t->card, false, "RESET");
    const uint32_t col = part_rgb(t, app->part);
    char v[16], w2[16];
    switch (app->edit_page) {
        case EDP_ENV: {
            float hx[3], hy[3];
            ui_env_handles(e->p, hx, hy);
            const float gx = ED_ENV_X, gy = ED_ENV_Y, gh = ED_ENV_H, bottom = gy + gh;
            sh_card(ED_BODY_X, ED_BODY_Y, ED_BODY_W, ED_ENV_H + 12, rgba(t->bg_a, 0xFF), rgba(t->ink, 0xFF), 0);
            sh_grid(ED_BODY_X + 2, ED_BODY_Y + 2, ED_BODY_W - 4, ED_ENV_H + 8, 12, rgba(t->ink, 0x18));
            const float sx = ED_ENV_X + ED_ENV_W * (0.28f + 0.24f + 0.14f);  // the end of the sustain plateau
            const float px[5] = {gx, hx[0], hx[1], sx, hx[2]}, py[5] = {bottom, hy[0], hy[1], hy[1], bottom};
            for (int i = 0; i < 4; i++) {  // the shape, as a fat line, filled faintly underneath
                const float dx = px[i + 1] - px[i], dy = py[i + 1] - py[i], len = sqrtf(dx * dx + dy * dy);
                sh_quad(px[i], py[i], px[i + 1], py[i + 1], px[i + 1], bottom, px[i], bottom, rgba(col, 0x40));
                if (len > 0.5f) sh_bar((px[i] + px[i + 1]) / 2, (py[i] + py[i + 1]) / 2, len, 3, atan2f(dy, dx), rgba(col, 0xFF));
            }
            static const EditParam hp[3] = {ED_ATTACK, ED_DECAY, ED_RELEASE};
            for (int h = 0; h < 3; h++) {
                const bool grab = app->edit_grab == h, set = (e->set >> hp[h]) & 1u;
                sh_circle(hx[h] + 1.5f, hy[h] + 1.5f, grab ? 7 : 5.5f, rgba(t->ink, 0xFF));
                sh_circle(hx[h], hy[h], grab ? 7 : 5.5f, rgba(set ? t->pal[0] : t->card, 0xFF));
            }
            // The four values underneath.
            static const EditParam vals[4] = {ED_ATTACK, ED_DECAY, ED_SUSTAIN, ED_RELEASE};
            static const char *const nm[4] = {"A", "D", "S", "R"};
            for (int i = 0; i < 4; i++) {
                edit_label(vals[i], e->p[vals[i]], v, sizeof(v));
                const bool set = (e->set >> vals[i]) & 1u;
                ui_text(ED_BODY_X + 24 + i * 48, ED_LOW_Y + 2, 0.32f, rgba(ink_on(t, t->card), set ? 0xFF : 0x90),
                        C2D_AlignCenter, "%s %s", nm[i], v);
            }
            break;
        }
        case EDP_FILTER:
            edit_xy(e, ED_CUTOFF, ED_RESO, col);
            edit_label(ED_FENV, e->p[ED_FENV], v, sizeof(v));
            edit_slider(ED_BODY_X, ED_LOW_Y, ED_BODY_W, e->p[ED_FENV], "ENV AMOUNT", v, (e->set >> ED_FENV) & 1u, true, col);
            break;
        case EDP_LFO:
            edit_xy(e, ED_LFO_RATE, ED_LFO_DEPTH, col);
            for (int i = 0; i < LFO_TARGETS; i++)
                edit_chip(ED_BODY_X + i * 48, ED_LOW_Y, 46, lfo_target_name(i), e->lfo_target == i, col);
            break;
        default: {  // VOICE
            const float y0 = ED_ROW_Y0, cw = ED_SLIDER_X - ED_BODY_X - 4;
            edit_chip(ED_BODY_X, y0, cw, e->mono ? "MONO" : "POLY", e->mono, col);
            edit_label(ED_GLIDE, e->p[ED_GLIDE], v, sizeof(v));
            edit_slider(ED_SLIDER_X, y0, ED_SLIDER_W, e->p[ED_GLIDE], "GLIDE", v, (e->set >> ED_GLIDE) & 1u, false, col);
            const int uk = sound_unison_kind(app->sound);
            if (uk == 1) {
                for (int u = 0; u < UNISON_MAX; u++) {
                    char lab[8];
                    snprintf(lab, sizeof(lab), "x%d", u + 1);
                    edit_chip(ED_BODY_X + u * (ED_CHIP_W + 3), y0 + ED_ROW_PITCH, ED_CHIP_W, lab, e->unison == u + 1, col);
                }
            } else {  // no unison chips: say why
                edit_chip(ED_BODY_X, y0 + ED_ROW_PITCH, cw, uk ? "x5 BUILT IN" : "NO UNISON", uk == 2, col);
            }
            edit_label(ED_DETUNE, e->p[ED_DETUNE], v, sizeof(v));
            edit_slider(ED_SLIDER_X, y0 + ED_ROW_PITCH, ED_SLIDER_W, e->p[ED_DETUNE], "DETUNE", v, (e->set >> ED_DETUNE) & 1u,
                        false, col);
            edit_label(ED_BRIGHT, e->p[ED_BRIGHT], v, sizeof(v));
            edit_slider(ED_BODY_X, y0 + 2 * ED_ROW_PITCH, ED_BODY_W, e->p[ED_BRIGHT], "BRIGHT", v, (e->set >> ED_BRIGHT) & 1u,
                        true, col);
            edit_chip(ED_BODY_X, y0 + 3 * ED_ROW_PITCH, cw, "MUTATE", false, colourful(t, 3));
            ui_text(ED_SLIDER_X + 4, y0 + 3 * ED_ROW_PITCH + 0.5f, 0.32f, rgba(ink_on(t, t->card), 0x90), 0, "nudge it all a little");
            for (int sn = 0; sn < 2; sn++)
                edit_chip(ED_BODY_X + sn * 37, y0 + 4 * ED_ROW_PITCH, 34, sn ? "B" : "A", (pe->snaps >> sn) & 1u, colourful(t, 1));
            snprintf(w2, sizeof(w2), "%d%%", (int)(pe->morph * 100 + 0.5f));
            edit_slider(ED_SLIDER_X, y0 + 4 * ED_ROW_PITCH, ED_SLIDER_W, pe->morph, "MORPH", w2, pe->snaps == 3, false, col);
            break;
        }
    }
}

static void draw_synth_page(const AppState *app) {
    const Theme *t = ui_theme();
    const Sound *s = sound_get(app->sound);
    draw_part_tabs(app);
    draw_sound_tiles(app);
    sh_card(SYN_PAD_X, SYN_PAD_Y, SYN_PAD_W, SYN_PAD_H, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 3);
    if (app->edit_view) {
        draw_edit_view(app);
        return;
    }
    if (app->kit_view) {
        draw_kit_view(app);
        return;
    }
    if (s->kind == SOUND_BANK)
        draw_bank_grid(app, s);
    else if (app->sound == sound_draw_index())
        draw_drawpad(app, s);
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

// FX automation: what each step of the pattern does to the FX, along the
// bottom of the pad.
static void draw_fx_auto(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqPattern *p = &app->pattern;
    bool any = false;
    const int off = app->bar_page * SEQ_STEPS;  // the bar on screen
    for (int s = 0; s < SEQ_STEPS_MAX; s++) any |= p->fx[s] != 0;
    if (!any && !app->seq.recording) return;
    for (int c = 0; c < SEQ_STEPS; c++) {
        const int s = off + c;
        const float x = FX_AUTO_X0 + c * FX_AUTO_PITCH;
        const int k1 = p->fx[s];
        const bool now = app->seq.playing && app->seq.step == s;
        sh_rect(x + 0.5f, FX_AUTO_Y, FX_AUTO_PITCH - 1.5f, FX_AUTO_H,
                k1 ? rgba(t->pal[k1 % THEME_PALETTE], 0xFF) : rgba(t->ink, (s & 3) == 0 ? 0x30 : 0x18));
        if (now) sh_rect(x + 0.5f, FX_AUTO_Y - 3, FX_AUTO_PITCH - 1.5f, 2, rgba(t->ink, 0xFF));
    }
    if (app->seq.recording && app->seq.playing)
        ui_text(FX_PAD_X + FX_PAD_W - 6, FX_PAD_Y + 4, 0.34f, rgba(t->pal[0], 0xFF), C2D_AlignRight, "REC MOVES");
}

static void draw_fx_page(const AppState *app) {
    draw_fx_stickers(app);
    draw_fx_pad(app);
    draw_fx_auto(app);
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
    ui_text(TAPE_TIME_X + 27, TAPE_BTN_Y + 3, 0.42f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter, "%02d:%02d.%d",
            whole / 60, whole % 60, (int)((tp->seconds - whole) * 10));
    static float reel;
    reel += tp->speed * 0.12f;
    draw_reel(TAPE_TIME_X + TAPE_TIME_W - 30, cy, 8, reel, t->pal[1]);
    draw_reel(TAPE_TIME_X + TAPE_TIME_W - 11, cy, 8, reel * 0.8f, t->pal[2]);

    // SAMPLE: a little waveform with scissors-ish snip marks.
    const uint32_t sf = colourful(t, 1);
    sticker(TAPE_SMP_X, TAPE_BTN_Y, TAPE_SMP_W, TAPE_BTN_H, sf, false, 0);
    const u32 si = rgba(ink_on(t, sf), 0xFF);
    for (int i = 0; i < 7; i++) {
        float hh = 2.0f + ((i * 5 + 3) % 7);
        sh_rect(TAPE_SMP_X + 5 + i * 4, cy - 3 - hh / 2, 2, hh, si);
    }
    ui_text(TAPE_SMP_X + TAPE_SMP_W / 2.0f, TAPE_BTN_Y + 10, 0.28f, si, C2D_AlignCenter, "SAMPLE");

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
    if (app->cursor_on) {  // story build: the pretend stylus
        const float cx = app->cursor_x, cy = app->cursor_y;
        if (app->cursor_down) {
            sh_ring(cx, cy, 9 + (s_frame % 10) * 0.6f, 2, rgba(t->pal[0], 0xC0));
            sh_circle(cx, cy, 4, rgba(t->pal[0], 0xFF));
        }
        // A stylus coming in from the bottom right, tip on the point.
        sh_bar(cx + 13, cy + 13, 30, 6, 0.785f, rgba(t->ink, 0xE0));
        sh_bar(cx + 12, cy + 12, 28, 3.5f, 0.785f, rgba(0xFFFFFF, 0xFF));
        sh_tri(cx + 2.5f, cy + 2.5f, 4, 3.927f, rgba(t->ink, 0xFF));
    }
    if (app->busy) {  // a slow job is about to run: say so
        sh_card(70, 80, 180, 50, rgba(t->pal[1], 0xFF), rgba(t->ink, 0xFF), 4);
        sh_floppy(84, 92, 26, rgba(t->pal[4], 0xFF), rgba(t->card, 0xFF), rgba(t->ink, 0xFF));
        ui_text(180, 94, 0.6f, rgba(ink_on(t, t->pal[1]), 0xFF), C2D_AlignCenter, "%s", app->busy);
    }
}
