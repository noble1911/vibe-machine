// ui_top.c -- the top screen: a vaporwave dusk scene in stereoscopic 3D.
//
// Back to front: gradient sky, striped sun, triangle mountains, a neon grid
// floor that scrolls with the output level, the live waveform, flying
// triangles (one burst per note), then the HUD at screen depth.
//
// 3D: every layer is drawn with a horizontal shift of eye * depth * MAX_SHIFT.
// Positive depth sits behind the screen, negative pops out of it.

#include <math.h>

#include "patch_names.h"
#include "ui.h"
#include "ui_style.h"

#define HORIZON 150.0f
#define MAX_SHIFT 7.0f

#define DEPTH_SUN 1.0f
#define DEPTH_MOUNTAINS 0.75f
#define DEPTH_FLOOR_FAR 0.65f
#define DEPTH_SCOPE 0.3f
#define DEPTH_TRIS -0.45f

#define MAX_TRIS 72

typedef struct {
    float x, y, vx, vy, angle, spin, r;
    int life, max_life;
    u32 rgb;
    bool outline;
} Tri;

static Tri s_tris[MAX_TRIS];
static float s_grid_phase;
static float s_level;
static unsigned s_frame;
static u32 s_rand = 0x1234567u;

static float frand(void) {
    s_rand = s_rand * 1664525u + 1013904223u;
    return (s_rand >> 8) / 16777216.0f;
}

static u32 key_rgb(int key) {
    static const u32 rgb[MUSIC_KEYS] = {VW_CYAN, VW_MINT, VW_YELLOW, VW_PURPLE,
                                        VW_PINK, VW_CYAN, VW_MINT, VW_YELLOW};
    return rgb[key & 7];
}

static void spawn(float x, float y, u32 rgb, float speed) {
    for (int i = 0; i < MAX_TRIS; i++) {
        Tri *t = &s_tris[i];
        if (t->life > 0) continue;
        t->x = x;
        t->y = y;
        t->vx = (frand() - 0.5f) * 2.4f * speed;
        t->vy = -(1.2f + frand() * 2.6f) * speed;
        t->angle = frand() * 6.283f;
        t->spin = (frand() - 0.5f) * 0.25f;
        t->r = 5.0f + frand() * 13.0f;
        t->max_life = t->life = 50 + (int)(frand() * 50);
        t->rgb = rgb;
        t->outline = frand() < 0.45f;
        return;
    }
}

void ui_key_struck(int key, PlayMode mode) {
    float x = TOP_W * (key + 1) / (float)(MUSIC_KEYS + 1);
    int n = mode == PLAY_DRUM ? 5 : mode == PLAY_CHORD ? 4 : 2;
    for (int i = 0; i < n; i++) {
        u32 rgb = mode == PLAY_DRUM ? (i & 1 ? VW_PINK : VW_YELLOW) : key_rgb(key + i * 2);
        spawn(x, 174.0f, rgb, mode == PLAY_DRUM ? 1.4f : 1.0f);
    }
}

void ui_top_init(void) {}

void ui_top_update(const AppState *app, const AudioStats *stats) {
    s_frame = app->frame;
    s_level = s_level * 0.8f + stats->level * 0.2f;
    s_grid_phase += 0.006f + s_level * 0.05f;
    if (s_grid_phase >= 1.0f) s_grid_phase -= 1.0f;

    // A lazy ambient drift so the scene is alive even in silence.
    if (s_frame % 45 == 0) spawn(frand() * TOP_W, 176.0f, key_rgb((int)(frand() * 8)), 0.45f);

    for (int i = 0; i < MAX_TRIS; i++) {
        Tri *t = &s_tris[i];
        if (t->life <= 0) continue;
        t->x += t->vx;
        t->y += t->vy;
        t->vx *= 0.985f;
        t->vy *= 0.985f;
        t->angle += t->spin;
        t->life--;
    }
}

// Sky gradient colour at height y, so sun stripes can "cut" the sun.
static u32 sky_rgb_at(float y, u8 alpha) {
    if (y < 90.0f) return vw_mix(VW_NIGHT, VW_PLUM, y / 90.0f, alpha);
    return vw_mix(VW_PLUM, VW_PINK, (y - 90.0f) / (HORIZON - 90.0f), alpha);
}

static void draw_sky(float eye) {
    const float star_dx = eye * DEPTH_SUN * MAX_SHIFT;
    C2D_DrawRectangle(0, 0, 0.5f, TOP_W, 90, vw(VW_NIGHT, 0xFF), vw(VW_NIGHT, 0xFF), vw(VW_PLUM, 0xFF),
                      vw(VW_PLUM, 0xFF));
    C2D_DrawRectangle(0, 90, 0.5f, TOP_W, HORIZON - 90, vw(VW_PLUM, 0xFF), vw(VW_PLUM, 0xFF), vw(VW_PINK, 0xFF),
                      vw(VW_PINK, 0xFF));
    // A few fixed stars.
    static const short stars[][2] = {{30, 20}, {88, 44}, {140, 14}, {262, 30}, {318, 12}, {372, 52}, {54, 70},
                                     {352, 82}, {196, 26}, {236, 60}};
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); i++) {
        float twinkle = 0.5f + 0.5f * sinf(s_frame * 0.05f + i * 1.7f);
        C2D_DrawRectSolid(stars[i][0] + star_dx, stars[i][1], 0.5f, 2, 2, vw(VW_LILAC, (u8)(80 + 150 * twinkle)));
    }
}

static void draw_sun(float eye) {
    float cx = TOP_W / 2.0f + eye * DEPTH_SUN * MAX_SHIFT, cy = 112.0f;
    float r = 54.0f + s_level * 6.0f;  // breathes with the music
    // A fan of triangles, coloured by height: yellow at the top, pink below.
    // (Built by hand: citro2d's ellipse primitive fails at this size in
    // Azahar, and the facets suit the low-poly look anyway.)
    const int segs = 48;
    const u32 centre = vw_mix(VW_YELLOW, VW_PINK, 0.5f, 0xFF);
    for (int i = 0; i < segs; i++) {
        float a0 = i * 6.2831853f / segs, a1 = (i + 1) * 6.2831853f / segs;
        float x0 = cx + cosf(a0) * r, y0 = cy + sinf(a0) * r;
        float x1 = cx + cosf(a1) * r, y1 = cy + sinf(a1) * r;
        C2D_DrawTriangle(cx, cy, centre, x0, y0, vw_mix(VW_YELLOW, VW_PINK, (y0 - cy + r) / (2 * r), 0xFF), x1,
                         y1, vw_mix(VW_YELLOW, VW_PINK, (y1 - cy + r) / (2 * r), 0xFF), 0.5f);
    }
    // Horizontal stripes cut through the lower half, thickening downwards.
    for (int i = 0; i < 7; i++) {
        float y = cy + 4.0f + i * 6.0f;
        float h = 1.0f + i * 0.8f;
        C2D_DrawRectSolid(cx - r - 2, y, 0.5f, 2 * r + 4, h, sky_rgb_at(y, 0xFF));
    }
}

static void draw_mountain(float x, float h, float w, float dx) {
    float peak_x = x + dx, base_l = x - w + dx, base_r = x + w + dx;
    u32 top = vw(VW_PURPLE, 0xFF), base = vw(VW_NIGHT, 0xFF);
    C2D_DrawTriangle(peak_x, HORIZON - h, top, base_l, HORIZON, base, base_r, HORIZON, base, 0.5f);
    u32 edge = vw(VW_CYAN, 0xC0);
    C2D_DrawLine(base_l, HORIZON, edge, peak_x, HORIZON - h, edge, 1.2f, 0.5f);
    C2D_DrawLine(peak_x, HORIZON - h, edge, base_r, HORIZON, edge, 1.2f, 0.5f);
    // Low-poly facet line.
    u32 facet = vw(VW_PINK, 0x70);
    C2D_DrawLine(peak_x, HORIZON - h, facet, x + w * 0.25f + dx, HORIZON, facet, 1.0f, 0.5f);
}

static void draw_mountains(float eye) {
    float dx = eye * DEPTH_MOUNTAINS * MAX_SHIFT;
    draw_mountain(34, 34, 46, dx);
    draw_mountain(96, 52, 50, dx);
    draw_mountain(150, 26, 36, dx);
    draw_mountain(256, 30, 40, dx);
    draw_mountain(314, 56, 54, dx);
    draw_mountain(374, 36, 44, dx);
}

static void draw_floor(float eye) {
    C2D_DrawRectangle(0, HORIZON, 0.5f, TOP_W, TOP_H - HORIZON, vw(VW_DUSK, 0xFF), vw(VW_DUSK, 0xFF),
                      vw(VW_NIGHT, 0xFF), vw(VW_NIGHT, 0xFF));
    // Glow line on the horizon.
    C2D_DrawRectSolid(0, HORIZON - 1, 0.5f, TOP_W, 2, vw(VW_PINK, 0xFF));

    const float far_dx = eye * DEPTH_FLOOR_FAR * MAX_SHIFT;
    const float cx = TOP_W / 2.0f;
    // Rails converge on the vanishing point; their far ends sit deeper in 3D.
    u32 rail = vw(VW_PINK, 0xB0);
    for (int i = -12; i <= 12; i++) {
        C2D_DrawLine(cx + i * 12.0f + far_dx, HORIZON, rail, cx + i * 64.0f, TOP_H, rail, 1.2f, 0.5f);
    }
    // Cross lines rush towards the viewer.
    const int n = 9;
    for (int j = 0; j < n; j++) {
        float t = (j + s_grid_phase) / n;  // 0 at horizon, 1 at the bottom
        float y = HORIZON + (TOP_H - HORIZON) * t * t;
        float dx = far_dx * (1.0f - t);
        C2D_DrawLine(0 + dx, y, vw(VW_CYAN, (u8)(60 + 180 * t)), TOP_W + dx, y, vw(VW_CYAN, (u8)(60 + 180 * t)),
                     1.0f + t, 0.5f);
    }
}

static void draw_scope(const int16_t *scope, float eye) {
    const float y0 = 40.0f, amp = 24.0f, dx = eye * DEPTH_SCOPE * MAX_SHIFT;
    const int step = 2;
    const float sx = (float)TOP_W / (SCOPE_POINTS - step);
    u32 glow = vw(VW_MINT, 0x40), core = vw(VW_MINT, 0xF0);
    for (int i = 0; i + step < SCOPE_POINTS; i += step) {
        float xa = i * sx + dx, xb = (i + step) * sx + dx;
        float ya = y0 - scope[i] / 32768.0f * amp;
        float yb = y0 - scope[i + step] / 32768.0f * amp;
        C2D_DrawLine(xa, ya, glow, xb, yb, glow, 4.0f, 0.5f);
        C2D_DrawLine(xa, ya, core, xb, yb, core, 1.4f, 0.5f);
    }
}

static void draw_tris(float eye) {
    float dx = eye * DEPTH_TRIS * MAX_SHIFT;
    for (int i = 0; i < MAX_TRIS; i++) {
        const Tri *t = &s_tris[i];
        if (t->life <= 0) continue;
        u8 a = (u8)(255.0f * t->life / t->max_life);
        if (t->outline) {
            ui_tri_outline(t->x + dx, t->y, t->r, t->angle, vw(t->rgb, a), 2.0f);
        } else {
            ui_tri(t->x + dx, t->y, t->r, t->angle, vw(t->rgb, a), vw(t->rgb, a), vw(VW_PINK, (u8)(a / 2)));
        }
    }
}

static void draw_load_meter(const AudioStats *stats) {
    const int segs = 10;
    const float x0 = TOP_W - 8 - segs * 7, y = 6;
    ui_text(x0 - 4, 3, 0.42f, vw(VW_LILAC, 0xFF), C2D_AlignRight, "CPU");
    int lit = (int)(stats->load * segs + 0.5f);
    int peak = (int)(stats->load_peak * segs + 0.5f);
    for (int i = 0; i < segs; i++) {
        u32 rgb = i < 6 ? VW_MINT : i < 8 ? VW_YELLOW : VW_PINK;
        u8 a = i < lit ? 0xFF : (i == peak - 1 ? 0xB0 : 0x38);
        C2D_DrawRectSolid(x0 + i * 7, y, 0.5f, 5, 9, vw(rgb, a));
    }
}

static void draw_hud(const AppState *app, const AudioStats *stats) {
    ui_text_fx(8, 2, 0.55f, vw(VW_YELLOW, 0xFF), vw(VW_PINK, 0xFF), 0, "C L A M S H E L L");

    static const char *const mode_names[] = {"NOTE", "CHORD", "DRUMS"};
    static const u32 mode_rgb[] = {VW_CYAN, VW_PURPLE, VW_PINK};
    float bx = TOP_W / 2.0f + 22;
    ui_bevel(bx - 52, 3, 104, 16, vw(mode_rgb[app->mode], 0xFF), false);
    ui_text(bx, 3, 0.42f, vw(VW_INK, 0xFF), C2D_AlignCenter, "%s  OCT%+d", mode_names[app->mode], app->octave);
    draw_load_meter(stats);

    // A Win95-style window holding the current patch.
    const float wx = 8, wy = 184, ww = TOP_W - 16, wh = 52;
    ui_bevel(wx, wy, ww, wh, vw(VW_LILAC, 0xFF), false);
    C2D_DrawRectangle(wx + 3, wy + 3, 0.5f, ww - 6, 13, vw(VW_PURPLE, 0xFF), vw(VW_PINK, 0xFF), vw(VW_PURPLE, 0xFF),
                      vw(VW_PINK, 0xFF));
    const char *bank = app->patch < 128 ? "JUNO" : "DX7";
    ui_text(wx + 7, wy + 2, 0.42f, vw(0xFFFFFF, 0xFF), 0, "%s_%03d.PAT", bank, app->patch);
    if (stats->late) ui_text(wx + ww - 22, wy + 2, 0.42f, vw(VW_YELLOW, 0xFF), C2D_AlignRight, "gaps %lu",
                             (unsigned long)stats->late);
    ui_bevel(wx + ww - 17, wy + 4, 12, 11, vw(VW_LILAC, 0xFF), false);
    ui_text(wx + ww - 11, wy + 2, 0.4f, vw(VW_INK, 0xFF), C2D_AlignCenter, "x");
    ui_text(wx + 8, wy + 18, 0.55f, vw(VW_INK, 0xFF), 0, "%s", patch_names[app->patch - PATCH_FIRST]);
    ui_text(wx + 8, wy + 35, 0.36f, vw(VW_PLUM, 0xFF), 0,
            "L chord  R drums  PAD patch/oct  SELECT bank  START quit");

    if (!app->audio_ok) {
        const float dx = 70, dy = 62, dw = TOP_W - 140, dh = 64;
        ui_bevel(dx, dy, dw, dh, vw(VW_LILAC, 0xFF), false);
        C2D_DrawRectangle(dx + 3, dy + 3, 0.5f, dw - 6, 13, vw(VW_PINK, 0xFF), vw(VW_PURPLE, 0xFF),
                          vw(VW_PINK, 0xFF), vw(VW_PURPLE, 0xFF));
        ui_text(dx + 7, dy + 2, 0.42f, vw(0xFFFFFF, 0xFF), 0, "ERROR");
        ui_text(TOP_W / 2.0f, dy + 22, 0.5f, vw(VW_INK, 0xFF), C2D_AlignCenter, "No audio: DSP firmware missing");
        ui_text(TOP_W / 2.0f, dy + 40, 0.42f, vw(VW_PLUM, 0xFF), C2D_AlignCenter, "Copy dspfirm.cdc to sdmc:/3ds/");
    }
}

void ui_draw_top(const AppState *app, const AudioStats *stats, const int16_t *scope, float eye) {
    draw_sky(eye);
    draw_sun(eye);
    draw_mountains(eye);
    draw_floor(eye);
    draw_scope(scope, eye);
    draw_tris(eye);
    draw_hud(app, stats);
}
