// ui_top.c -- the top screen, in stereoscopic 3D.
//
// Two backdrops, chosen by the theme:
//  * PRISM (most themes): layers of see-through triangles at three depths,
//    scanline bands, speckles and drifting Memphis motifs.
//  * SUNSET (Vapor Sunset theme): striped sun, low-poly mountains and a
//    neon grid floor.
// Over either: a thick waveform with a hard shadow, bursts of motifs on
// every note (they pop out of the screen), and a sticker-style HUD.
//
// 3D: each layer shifts horizontally by eye * depth * MAX_SHIFT. Positive
// depth sits behind the screen, negative floats in front of it.

#include <math.h>
#include <string.h>

#include "motif.h"
#include "patch_names.h"
#include "shapes.h"
#include "sounds.h"
#include "ui.h"
#include "ui_style.h"

#define MAX_SHIFT 7.0f
#define DEPTH_FAR 1.0f
#define DEPTH_MID 0.55f
#define DEPTH_NEAR 0.15f
#define DEPTH_MOTIFS 0.35f
#define DEPTH_SCOPE 0.25f
#define DEPTH_BURST -0.45f

#define BIG_TRIS 14
#define TOP_MOTIFS 14
#define MAX_BURST 64
#define TOAST_FRAMES 100

typedef struct {
    float x, y, r, angle, spin, depth;
    int c0, c1;
    bool striped;
    uint8_t alpha;
} BigTri;

static BigTri s_big[BIG_TRIS];
static Motif s_motifs[TOP_MOTIFS];
static Motif s_burst[MAX_BURST];
static Rng s_rng = {0xBEEFu};
static float s_level;
static unsigned s_frame;
static float s_anim;      // scene clock: runs at tape speed, stops while stuttering
static float s_time_acc;  // whole motion steps owed at the current speed
static int s_toast;
static bool s_started;

static float shift(float eye, float depth) { return eye * depth * MAX_SHIFT; }

// --------------------------------------------------------------- state ----

void ui_top_init(void) {}

void ui_top_theme_changed(void) {
    const Theme *t = ui_theme();
    Rng rng = {0x9E3779B9u ^ (uint32_t)(ui_theme_index() * 7919 + 1)};
    for (int i = 0; i < BIG_TRIS; i++) {
        BigTri *b = &s_big[i];
        b->depth = i < 5 ? DEPTH_FAR : i < 10 ? DEPTH_MID : DEPTH_NEAR;
        b->x = rng_range(&rng, -20.0f, TOP_W + 20.0f);
        b->y = rng_range(&rng, -30.0f, TOP_H - 20.0f);
        b->r = rng_range(&rng, 40.0f, 120.0f) * (b->depth == DEPTH_NEAR ? 0.7f : 1.0f);
        b->angle = rng_range(&rng, 0.0f, 6.283f);
        b->spin = rng_range(&rng, -0.003f, 0.003f);
        b->c0 = rng_int(&rng, THEME_PALETTE);
        b->c1 = (b->c0 + 1 + rng_int(&rng, THEME_PALETTE - 1)) % THEME_PALETTE;
        b->striped = rng_f(&rng) < 0.3f;
        b->alpha = (uint8_t)rng_range(&rng, 140.0f, 210.0f);
    }
    for (int i = 0; i < TOP_MOTIFS; i++) {
        bool spot = t->spots && rng_f(&rng) < 0.7f;
        uint32_t kinds = spot ? MO_BIT(MO_SPOT) : (MO_ALL & ~MO_BIT(MO_SPOT));
        s_motifs[i] = motif_random(&rng, t, rng_f(&rng) * TOP_W, rng_f(&rng) * 180.0f, spot ? 14.0f : 6.0f,
                                   spot ? 24.0f : 12.0f, kinds);
    }
    if (s_started) s_toast = TOAST_FRAMES;
    s_started = true;
}

static Motif *free_burst(void) {
    for (int i = 0; i < MAX_BURST; i++)
        if (s_burst[i].life <= 0) return &s_burst[i];
    return 0;
}

static void spawn_burst(float x, float y, float speed, float size_lo, float size_hi) {
    Motif *m = free_burst();
    if (!m) return;
    *m = motif_random(&s_rng, ui_theme(), x, y, size_lo, size_hi, MO_PUNCHY);
    m->vx = rng_range(&s_rng, -1.3f, 1.3f) * speed;
    m->vy = -rng_range(&s_rng, 1.4f, 3.6f) * speed;
    m->spin = rng_range(&s_rng, -0.15f, 0.15f);
    m->max_life = m->life = 55 + rng_int(&s_rng, 40);
}

void ui_key_struck(int key, PlayMode mode) {
    ui_bump_kick();
    float x = TOP_W * (key + 1) / (float)(MUSIC_KEYS + 1);
    bool drum = mode == PLAY_DRUM;
    int n = drum ? 5 : mode == PLAY_CHORD ? 4 : 3;
    for (int i = 0; i < n; i++) spawn_burst(x, 160.0f, drum ? 1.4f : 1.0f, drum ? 9.0f : 7.0f, drum ? 17.0f : 14.0f);
}

void ui_seq_struck(int key, bool drum) {
    ui_bump_kick();
    float x = TOP_W * (key + 1) / (float)(MUSIC_KEYS + 1);
    for (int i = 0; i < 2; i++) spawn_burst(x, 160.0f, drum ? 1.1f : 0.9f, 6.0f, drum ? 13.0f : 11.0f);
}

void ui_top_update(const AppState *app, const AudioStats *stats) {
    s_frame = app->frame;
    s_level = s_level * 0.8f + stats->level * 0.2f;
    if (s_toast > 0) s_toast--;

    // The scene obeys the time effects: STUTTER freezes it, TAPE slows it
    // down with the tape. Motion runs in whole steps owed to the clock.
    float speed = app->fx.on[FX_STUTTER] ? 0.0f : app->fx.tape_speed;
    s_anim += speed;
    s_time_acc += speed;
    while (s_time_acc >= 1.0f) {
        s_time_acc -= 1.0f;
        for (int i = 0; i < BIG_TRIS; i++) s_big[i].angle += s_big[i].spin;
        for (int i = 0; i < TOP_MOTIFS; i++) motif_step(&s_motifs[i], TOP_W, TOP_H);
        for (int i = 0; i < MAX_BURST; i++)
            if (s_burst[i].life > 0) motif_step(&s_burst[i], TOP_W, TOP_H);
    }

    // A lazy ambient drift keeps the screen alive in silence.
    if (speed > 0.5f && s_frame % 50 == 0) spawn_burst(rng_f(&s_rng) * TOP_W, 162.0f, 0.4f, 6.0f, 11.0f);
}

// --------------------------------------------------------- prism scene ----

static void draw_prism(float eye) {
    const Theme *t = ui_theme();
    uint32_t mid = rgb_lerp(t->sky_a, t->sky_b, 0.5f);
    C2D_DrawRectangle(0, 0, 0.5f, TOP_W, TOP_H, rgba(t->sky_a, 0xFF), rgba(mid, 0xFF), rgba(mid, 0xFF),
                      rgba(t->sky_b, 0xFF));
    if (t->grid) sh_grid(-16 + shift(eye, DEPTH_FAR), -8, TOP_W + 32, TOP_H + 16, 16, rgba(t->grid_rgb, 0xFF));

    // Big see-through triangles, far layer first; they swell with the music.
    for (int i = 0; i < BIG_TRIS; i++) {
        const BigTri *b = &s_big[i];
        float r = b->r * (1.0f + s_level * 0.22f * (1.2f - b->depth));
        float x = b->x + shift(eye, b->depth);
        if (b->striped) {
            sh_tri_striped(x, b->y, r, b->angle, 7.0f, 3.5f, rgba(t->pal[b->c0], b->alpha));
        } else {
            uint32_t blend = rgb_lerp(t->pal[b->c0], t->pal[b->c1], 0.5f);
            sh_tri_grad(x, b->y, r, b->angle, rgba(t->pal[b->c0], b->alpha), rgba(t->pal[b->c1], b->alpha),
                        rgba(blend, b->alpha));
        }
    }

    // Scanline bands drift down the screen.
    for (int i = 0; i < 3; i++) {
        float y = fmodf(i * 90.0f + s_anim * (0.2f + 0.1f * i), TOP_H + 40.0f) - 30.0f;
        sh_scanlines(0, y, TOP_W, 30, 3, 1, rgba(t->fleck, 0x38));
    }
    sh_speckles(shift(eye, DEPTH_NEAR), 0, TOP_W, TOP_H, 70, 0xF1EC5u + ui_theme_index(), rgba(t->fleck, 0xA0));

    for (int i = 0; i < TOP_MOTIFS; i++) motif_draw(&s_motifs[i], shift(eye, DEPTH_MOTIFS), 1.0f, 0xFF, t->ink, 2.0f);
}

// -------------------------------------------------------- sunset scene ----

#define HORIZON 150.0f
#define VW_PINK 0xFF71CE
#define VW_CYAN 0x01CDFE
#define VW_PURPLE 0xB967FF
#define VW_YELLOW 0xFFFB96
#define VW_NIGHT 0x120A2A
#define VW_DUSK 0x2B0F4A
#define VW_PLUM 0x5A1E7A
#define VW_LILAC 0xD9C6F5

static uint32_t sky_rgb_at(float y) {
    if (y < 90.0f) return rgb_lerp(VW_NIGHT, VW_PLUM, y / 90.0f);
    return rgb_lerp(VW_PLUM, VW_PINK, (y - 90.0f) / (HORIZON - 90.0f));
}

static void draw_sunset(float eye) {
    C2D_DrawRectangle(0, 0, 0.5f, TOP_W, 90, rgba(VW_NIGHT, 0xFF), rgba(VW_NIGHT, 0xFF), rgba(VW_PLUM, 0xFF),
                      rgba(VW_PLUM, 0xFF));
    C2D_DrawRectangle(0, 90, 0.5f, TOP_W, HORIZON - 90, rgba(VW_PLUM, 0xFF), rgba(VW_PLUM, 0xFF),
                      rgba(VW_PINK, 0xFF), rgba(VW_PINK, 0xFF));
    sh_speckles(shift(eye, DEPTH_FAR), 0, TOP_W, 90, 24, 0x57A25u, rgba(VW_LILAC, 0xC0));

    // Striped sun: a fan of height-graded triangles, cut by sky-coloured bands.
    float cx = TOP_W / 2.0f + shift(eye, DEPTH_FAR), cy = 112.0f, r = 54.0f + s_level * 6.0f;
    const int segs = 48;
    uint32_t centre = rgb_lerp(VW_YELLOW, VW_PINK, 0.5f);
    for (int i = 0; i < segs; i++) {
        float a0 = i * 6.2831853f / segs, a1 = (i + 1) * 6.2831853f / segs;
        float x0 = cx + cosf(a0) * r, y0 = cy + sinf(a0) * r, x1 = cx + cosf(a1) * r, y1 = cy + sinf(a1) * r;
        C2D_DrawTriangle(cx, cy, rgba(centre, 0xFF), x0, y0,
                         rgba(rgb_lerp(VW_YELLOW, VW_PINK, (y0 - cy + r) / (2 * r)), 0xFF), x1, y1,
                         rgba(rgb_lerp(VW_YELLOW, VW_PINK, (y1 - cy + r) / (2 * r)), 0xFF), 0.5f);
    }
    for (int i = 0; i < 7; i++) {
        float y = cy + 4.0f + i * 6.0f;
        sh_rect(cx - r - 2, y, 2 * r + 4, 1.0f + i * 0.8f, rgba(sky_rgb_at(y), 0xFF));
    }

    // Low-poly mountains.
    static const float mountains[][3] = {{34, 34, 46}, {96, 52, 50}, {150, 26, 36},
                                         {256, 30, 40}, {314, 56, 54}, {374, 36, 44}};
    float mdx = shift(eye, 0.75f);
    for (int i = 0; i < 6; i++) {
        float px = mountains[i][0] + mdx, h = mountains[i][1], w = mountains[i][2];
        C2D_DrawTriangle(px, HORIZON - h, rgba(VW_PURPLE, 0xFF), px - w, HORIZON, rgba(VW_NIGHT, 0xFF), px + w,
                         HORIZON, rgba(VW_NIGHT, 0xFF), 0.5f);
        u32 edge = rgba(VW_CYAN, 0xC0);
        C2D_DrawLine(px - w, HORIZON, edge, px, HORIZON - h, edge, 1.2f, 0.5f);
        C2D_DrawLine(px, HORIZON - h, edge, px + w, HORIZON, edge, 1.2f, 0.5f);
    }

    // Grid floor rushing towards the viewer.
    C2D_DrawRectangle(0, HORIZON, 0.5f, TOP_W, TOP_H - HORIZON, rgba(VW_DUSK, 0xFF), rgba(VW_DUSK, 0xFF),
                      rgba(VW_NIGHT, 0xFF), rgba(VW_NIGHT, 0xFF));
    sh_rect(0, HORIZON - 1, TOP_W, 2, rgba(VW_PINK, 0xFF));
    float far_dx = shift(eye, 0.65f);
    u32 rail = rgba(VW_PINK, 0xB0);
    for (int i = -12; i <= 12; i++)
        C2D_DrawLine(TOP_W / 2.0f + i * 12.0f + far_dx, HORIZON, rail, TOP_W / 2.0f + i * 64.0f, TOP_H, rail, 1.2f,
                     0.5f);
    float phase = fmodf(s_anim * 0.006f + s_level * 0.5f, 1.0f);
    for (int j = 0; j < 9; j++) {
        float tt = (j + phase) / 9.0f, y = HORIZON + (TOP_H - HORIZON) * tt * tt, dx = far_dx * (1.0f - tt);
        u32 c = rgba(VW_CYAN, (uint8_t)(60 + 180 * tt));
        C2D_DrawLine(dx, y, c, TOP_W + dx, y, c, 1.0f + tt, 0.5f);
    }
}

// ------------------------------------------------------ shared overlay ----

static void draw_scope(const int16_t *scope, float eye) {
    const Theme *t = ui_theme();
    uint32_t line = t->on_bg, shade = t->pal[0] == line ? t->pal[1] : t->pal[0];
    const float y0 = 96.0f, amp = 36.0f, dx = shift(eye, DEPTH_SCOPE);
    const int step = 2;
    const float sx = (float)TOP_W / (SCOPE_POINTS - step);
    for (int pass = 0; pass < 2; pass++) {
        float off = pass == 0 ? 2.5f : 0.0f;
        u32 c = pass == 0 ? rgba(shade, 0xE0) : rgba(line, 0xFF);
        for (int i = 0; i + step < SCOPE_POINTS; i += step) {
            float ya = y0 - scope[i] / 32768.0f * amp, yb = y0 - scope[i + step] / 32768.0f * amp;
            C2D_DrawLine(i * sx + dx + off, ya + off, c, (i + step) * sx + dx + off, yb + off, c, 3.0f, 0.5f);
        }
    }
}

static void draw_bursts(float eye) {
    const Theme *t = ui_theme();
    float dx = shift(eye, DEPTH_BURST);
    for (int i = 0; i < MAX_BURST; i++) {
        const Motif *m = &s_burst[i];
        if (m->life <= 0) continue;
        int age = m->max_life - m->life;
        float scale = age < 8 ? 0.4f + 0.6f * age / 8.0f : 1.0f;  // pop in
        float fade = m->life < 20 ? m->life / 20.0f : 1.0f;      // fade out
        motif_draw(m, dx, scale, (uint8_t)(255 * fade), t->ink, 2.5f);
    }
}

// Ransom-note title: one sticker per letter, slightly jumbled.
static void draw_title(void) {
    const Theme *t = ui_theme();
    static const char title[] = "VIBE MACHINE";
    static const int8_t jitter[] = {1, 3, 0, 2, 0, 3, 1, 0, 2, 3, 0, 1};
    float x = 6;
    for (int i = 0; title[i]; i++) {
        if (title[i] == ' ') {
            x += 6;
            continue;
        }
        uint32_t face = t->pal[i % THEME_PALETTE];
        float y = 3 + jitter[i];
        sh_card(x, y, 13, 16, rgba(face, 0xFF), rgba(t->ink, 0xFF), 2);
        ui_text(x + 6.5f, y + 0.5f, 0.42f, rgba(ink_on(t, face), 0xFF), C2D_AlignCenter, "%c", title[i]);
        x += 14;
    }
}

// Transport sticker plus 16 step stickers; the playing step jumps up.
static void draw_steps(const AppState *app) {
    const Theme *t = ui_theme();
    const SeqStatus *st = &app->seq;
    const u32 ink = rgba(t->ink, 0xFF);
    const float y = 165;

    uint32_t face = st->playing ? t->pal[3] : t->card;
    sh_card(6, y - 1, 40, 16, rgba(face, 0xFF), ink, 2);
    u32 icon = rgba(ink_on(t, face), 0xFF);
    if (st->playing)
        sh_rect(11, y + 3, 7, 7, icon);
    else
        sh_tri(15, y + 6.5f, 5, 0.0f, icon);
    if (st->recording && ((s_frame / 15) & 1)) sh_circle(15, y + 6.5f, 3, rgba(t->pal[0], 0xFF));
    ui_text(33, y - 0.5f, 0.38f, icon, C2D_AlignCenter, "%d", st->bpm);

    for (int s = 0; s < SEQ_STEPS; s++) {
        const bool head = st->playing && s == st->step;
        int dummy[1];
        const bool full = app->pattern.drums[s] || seq_notes_at(&app->pattern, -1, s, dummy, 1) > 0;
        float x = 52 + s * 21.5f, lift = head ? -5.0f : 0.0f;
        uint32_t f = head ? t->pal[0] : full ? t->pal[1 + (s / 4) % 4] : t->card;
        sh_card(x, y + lift, 18, 14, rgba(f, full || head ? 0xFF : 0xC0), ink, head ? 3 : 1.5f);
        if (s % 4 == 0) sh_circle(x + 9, y + 7 + lift, 1.8f, rgba(ink_on(t, f), 0xFF));
    }
}

// The song's name on a sticker under the title, with * for unsaved changes.
static void draw_song_name(const AppState *app) {
    const Theme *t = ui_theme();
    const char *name = app->song_name[0] ? app->song_name : "NEW SONG";
    float w = 34.0f + strlen(name) * 7.4f + (app->song_dirty ? 8.0f : 0.0f);
    sh_card(6, 24, w, 17, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 2);
    sh_floppy(10, 26, 13, rgba(t->pal[2], 0xFF), rgba(t->card, 0xFF), rgba(t->ink, 0xFF));
    ui_text(28, 24.5f, 0.42f, rgba(ink_on(t, t->card), 0xFF), 0, "%s%s", name, app->song_dirty ? " *" : "");
    if (app->seq.arr_row >= 0 && app->arr.count > 0) {  // where the arrangement is
        const SeqArrRow *r = &app->arr.rows[app->seq.arr_row];
        const uint32_t f = t->pal[r->slot % THEME_PALETTE];
        sh_card(w + 12, 24, 78, 17, rgba(f, 0xFF), rgba(t->ink, 0xFF), 2);
        ui_text(w + 51, 24.5f, 0.4f, rgba(ink_on(t, f), 0xFF), C2D_AlignCenter, "ROW %d/%d %c", app->seq.arr_row + 1,
                app->arr.count, 'A' + r->slot);
    }
}

static void draw_hud(const AppState *app, const AudioStats *stats) {
    const Theme *t = ui_theme();
    draw_title();
    draw_song_name(app);
    draw_steps(app);
    if (app->mixdown) {  // recording the mixdown: say so, under the CPU meter
        const int secs = (int)app->mixdown_secs;
        const uint32_t f = ((s_frame / 15) & 1) ? t->pal[0] : t->card;
        sh_card(288, 24, 106, 17, rgba(f, 0xFF), rgba(t->ink, 0xFF), 2);
        sh_circle(297, 32.5f, 4, rgba(f == t->pal[0] ? t->ink : t->pal[0], 0xFF));
        ui_text(346, 24.5f, 0.42f, rgba(ink_on(t, f), 0xFF), C2D_AlignCenter, "%s %d:%02d",
                app->mixdown == 2 ? "TAIL" : "MIXDOWN", secs / 60, secs % 60);
    }

    static const char *const mode_names[] = {"NOTE", "CHORD", "DRUMS"};
    uint32_t mode_face = t->pal[app->mode == PLAY_NOTE ? 1 : app->mode == PLAY_CHORD ? 4 : 0];
    sh_card(176, 3, 104, 17, rgba(mode_face, 0xFF), rgba(t->ink, 0xFF), 2);
    // Mode, octave, and the pattern slot (with an arrow when a song is chaining).
    ui_text(228, 3, 0.42f, rgba(ink_on(t, mode_face), 0xFF), C2D_AlignCenter, "%s OCT%+d %c%s", mode_names[app->mode],
            app->octave, 'A' + app->seq.slot, app->seq.song ? ">" : "");

    // CPU meter sticker.
    sh_card(288, 3, 106, 17, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 2);
    ui_text(293, 3.5f, engine_two_cores() ? 0.34f : 0.4f, rgba(ink_on(t, t->card), 0xFF), 0, engine_two_cores() ? "CPUx2" : "CPU");
    int lit = (int)(stats->load * 10 + 0.5f), peak = (int)(stats->load_peak * 10 + 0.5f);
    for (int i = 0; i < 10; i++) {
        uint32_t c = i < 6 ? t->pal[1] : i < 8 ? t->pal[3] : t->pal[0];
        uint8_t a = i < lit ? 0xFF : (i == peak - 1 ? 0xB0 : 0x30);
        sh_rect(322 + i * 7, 7, 5, 9, rgba(c, a));
    }
    if (stats->late) ui_text(392, 22, 0.36f, rgba(t->on_bg, 0xFF), C2D_AlignRight, "gaps %lu",
                             (unsigned long)stats->late);

    // Patch card.
    const float wx = 8, wy = 186, ww = TOP_W - 16, wh = 48;
    sh_card(wx, wy, ww, wh, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 3);
    sh_rect(wx + 2, wy + 2, ww - 4, 13, rgba(t->pal[0], 0xFF));
    const TapeStatus *tp = &app->tape;
    if (tp->playing || tp->used[0] || tp->used[1] || tp->used[2] || tp->used[3]) {
        // Tape counter in the title bar, with a blinking dot while recording.
        int whole = (int)tp->seconds;
        ui_text(wx + ww - 8, wy + 1, 0.4f, rgba(ink_on(t, t->pal[0]), 0xFF), C2D_AlignRight, "TAPE %d  %02d:%02d.%d %s",
                tp->track + 1, whole / 60, whole % 60, (int)((tp->seconds - whole) * 10),
                tp->recording ? "REC" : tp->playing ? (tp->speed < 0 ? "REV" : "PLAY") : "");
        if (tp->recording && ((s_frame / 12) & 1)) sh_circle(wx + ww - 150, wy + 8.5f, 3.5f, rgba(t->ink, 0xFF));
    } else {
        sh_zigzag(wx + ww - 40, wy + 8.5f, 60, 2.5f, 0, 1.6f, 10, rgba(ink_on(t, t->pal[0]), 0x90));
    }
    const Sound *snd = sound_get(app->sound);
    const uint32_t bar = ink_on(t, t->pal[0]), text = ink_on(t, t->card);
    if (snd->kind == SOUND_BANK) {
        ui_text(wx + 6, wy + 1, 0.4f, rgba(bar, 0xFF), 0, "PART %d  %s_%03d.PAT", app->part + 1, snd->name,
                app->patch - snd->bank_first);
        ui_text(wx + 8, wy + 16, 0.58f, rgba(text, 0xFF), 0, "%s", patch_names[app->patch - PATCH_FIRST]);
    } else {
        ui_text(wx + 6, wy + 1, 0.4f, rgba(bar, 0xFF), 0, "PART %d  %s.SYN", app->part + 1, snd->name);
        ui_text(wx + 8, wy + 16, 0.58f, rgba(text, 0xFF), 0, "%s  %s %d  %s %d", snd->name, snd->macro_name[0],
                (int)(app->macro[0] * 100 + 0.5f), snd->macro_name[1], (int)(app->macro[1] * 100 + 0.5f));
    }
    ui_text(wx + 8, wy + 33, 0.36f, rgba(text, 0xB0), 0,
            "L chord  R drums  PAD sound/oct  SELECT next sound  START play  hold START quit");
    // The sound's mascot rides on the card and squishes on every note.
    Motif mascot = {0};
    mascot.kind = snd->motif;
    mascot.x = wx + ww - 24;
    mascot.y = wy + 28;
    mascot.size = 11.0f * (1.0f + ui_bump() * 0.3f);
    mascot.angle = -1.5708f + sinf(s_frame * 0.05f) * 0.15f;
    mascot.rgb = t->pal[(app->sound + 1) % THEME_PALETTE];
    mascot.seed = (uint32_t)app->sound * 31u + 7u;
    motif_draw(&mascot, 0, 1.0f, 0xFF, t->ink, 2.0f);
    sh_eyes(mascot.x, mascot.y, 2.6f, sinf(s_frame * 0.03f) * 0.8f, 0, (s_frame % 190) < 6, rgba(t->ink, 0xFF));

    if (app->busy) {
        sh_card(120, 70, 160, 40, rgba(t->pal[1], 0xFF), rgba(t->ink, 0xFF), 4);
        ui_text(200, 78, 0.6f, rgba(ink_on(t, t->pal[1]), 0xFF), C2D_AlignCenter, "%s", app->busy);
    }

    if (app->mic_rec) {  // a big REC card with a live level meter
        sh_card(110, 60, 180, 52, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 4);
        sh_circle(132, 78, 8.0f + ((s_frame / 8) & 1) * 1.5f, rgba(t->pal[0], 0xFF));
        ui_text(148, 64, 0.6f, rgba(ink_on(t, t->card), 0xFF), 0, "REC  %.1fs", app->mic_secs);
        for (int i = 0; i < 16; i++) {
            bool lit = i < (int)(app->mic_level * 20.0f);
            sh_rect(122 + i * 10, 96, 8, 9, rgba(t->pal[i < 10 ? 1 : i < 13 ? 3 : 0], lit ? 0xFF : 0x30));
        }
    }

    if (s_toast > 0) {
        int age = TOAST_FRAMES - s_toast;
        float drop = age < 10 ? (10 - age) * 4.0f : 0.0f;
        float y = 62 - drop;
        sh_card(90, y, 220, 46, rgba(t->pal[1], 0xFF), rgba(t->ink, 0xFF), 4);
        uint32_t ink = ink_on(t, t->pal[1]);
        ui_text(200, y + 4, 0.4f, rgba(ink, 0xFF), C2D_AlignCenter, "THEME %d / %d", ui_theme_index() + 1,
                ui_theme_count());
        ui_text(200, y + 18, 0.75f, rgba(ink, 0xFF), C2D_AlignCenter, "%s", t->name);
    }

    if (!app->audio_ok) {
        sh_card(70, 120, TOP_W - 140, 52, rgba(t->card, 0xFF), rgba(t->ink, 0xFF), 4);
        sh_rect(72, 122, TOP_W - 144, 13, rgba(t->pal[0], 0xFF));
        ui_text(78, 121, 0.4f, rgba(ink_on(t, t->pal[0]), 0xFF), 0, "ERROR");
        ui_text(TOP_W / 2.0f, 138, 0.48f, rgba(ink_on(t, t->card), 0xFF), C2D_AlignCenter,
                "No audio: DSP firmware missing");
        ui_text(TOP_W / 2.0f, 154, 0.4f, rgba(ink_on(t, t->card), 0xC0), C2D_AlignCenter,
                "Copy dspfirm.cdc to sdmc:/3ds/");
    }
}

// The scene's reaction to the FX page (drawn over the scene, under the HUD).
static void draw_fx_overlay(const AppState *app) {
    const Theme *t = ui_theme();
    const FxState *fx = &app->fx;
    if (fx->on[FX_SWEEP]) {
        float x = fx->x[FX_SWEEP];
        if (x < 0.48f)  // low-pass: the lights go down
            sh_rect(0, 0, TOP_W, TOP_H, rgba(t->ink, (uint8_t)((0.48f - x) / 0.48f * 0xB0)));
        else if (x > 0.52f)  // high-pass: a bright, thin flash
            sh_rect(0, 0, TOP_W, TOP_H, rgba(0xFFFFFF, (uint8_t)((x - 0.52f) / 0.48f * 0x70)));
    }
    if (fx->on[FX_CRUSH]) {  // pixelate: a flickering mosaic, chunkier with RATE
        float cell = 6.0f + fx->x[FX_CRUSH] * 18.0f;
        Rng r = {(s_frame / 3) * 2654435761u + 1u};
        for (float y = 0; y < TOP_H; y += cell)
            for (float x = 0; x < TOP_W; x += cell)
                if (rng_f(&r) < 0.35f)
                    sh_rect(x, y, cell, cell, rgba(t->pal[rng_int(&r, THEME_PALETTE)], (uint8_t)(0x20 + fx->y[FX_CRUSH] * 0x50)));
    }
    if (fx->on[FX_STUTTER]) {  // the frozen frame glitches in slices
        Rng r = {(s_frame / 4) * 40503u + 7u};
        for (int i = 0; i < 5; i++) {
            float y = rng_f(&r) * TOP_H, h = 3 + rng_f(&r) * 12;
            sh_rect(rng_range(&r, -30, 10), y, TOP_W + 20, h, rgba(t->pal[rng_int(&r, THEME_PALETTE)], 0x90));
        }
    }
    if (fx->tape_speed < 1.0f)  // the tape winds down: everything dims with it
        sh_rect(0, 0, TOP_W, TOP_H, rgba(t->ink, (uint8_t)((1.0f - fx->tape_speed) * 0xA0)));
}

// Story build: what's happening, in a big sticker along the bottom.
static void draw_caption(const AppState *app) {
    if (!app->caption) return;
    const Theme *t = ui_theme();
    const float y = TOP_H - 34, h = 28;
    const uint32_t face = t->pal[(app->caption_n + 2) % THEME_PALETTE];
    sh_card(8, y, TOP_W - 16, h, rgba(face, 0xFF), rgba(t->ink, 0xFF), 4);
    float tx = 18;
    if (app->caption_n > 0) {  // the step number in a circle
        sh_circle(24, y + h / 2.0f, 11, rgba(t->ink, 0xFF));
        ui_text(24, y + 5, 0.55f, rgba(face, 0xFF), C2D_AlignCenter, "%d", app->caption_n);
        tx = 42;
    }
    ui_text(tx, y + 6.5f, 0.46f, rgba(ink_on(t, face), 0xFF), 0, "%s", app->caption);
}

void ui_draw_top(const AppState *app, const AudioStats *stats, const int16_t *scope, float eye) {
    if (ui_theme()->sunset)
        draw_sunset(eye);
    else
        draw_prism(eye);
    draw_scope(scope, eye);
    draw_bursts(eye);
    draw_fx_overlay(app);
    draw_hud(app, stats);
    draw_caption(app);
}
