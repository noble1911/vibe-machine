// ui_bottom.c -- the touchscreen: Memphis-confetti backdrop, four neon
// gradient sliders with triangle pointers, and eight bevelled keys.


#include "ui.h"
#include "ui_style.h"

#define CONFETTI 30

typedef struct {
    short x, y;
    u8 kind;  // 0 triangle, 1 dot, 2 squiggle, 3 cross
    u8 colour;
    float angle;
} Confetti;

static Confetti s_confetti[CONFETTI];

static const char *const kButtonHints[MUSIC_KEYS] = {"<", "v", ">", "^", "Y", "B", "A", "X"};
static const u32 kConfettiRgb[] = {VW_PINK, VW_CYAN, VW_MINT, VW_PURPLE, VW_YELLOW};

void ui_bottom_init(void) {
    // Fixed seed: the pattern is part of the design, the same every boot.
    u32 seed = 0xC1A55u;
    for (int i = 0; i < CONFETTI; i++) {
        seed = seed * 1664525u + 1013904223u;
        s_confetti[i].x = (short)((seed >> 8) % BOTTOM_W);
        seed = seed * 1664525u + 1013904223u;
        s_confetti[i].y = (short)((seed >> 8) % BOTTOM_H);
        s_confetti[i].kind = (u8)((seed >> 4) % 4);
        s_confetti[i].colour = (u8)((seed >> 12) % 5);
        s_confetti[i].angle = ((seed >> 16) % 628) / 100.0f;
    }
}

static void draw_confetti(void) {
    for (int i = 0; i < CONFETTI; i++) {
        const Confetti *c = &s_confetti[i];
        u32 col = vw(kConfettiRgb[c->colour], 0x48);
        switch (c->kind) {
            case 0: ui_tri_outline(c->x, c->y, 6, c->angle, col, 1.5f); break;
            case 1: C2D_DrawCircleSolid(c->x, c->y, 0.5f, 2.5f, col); break;
            case 2:  // zig-zag squiggle
                for (int s = 0; s < 3; s++) {
                    float x0 = c->x + s * 5.0f, y0 = c->y + (s & 1 ? 4.0f : 0.0f);
                    float x1 = x0 + 5.0f, y1 = c->y + (s & 1 ? 0.0f : 4.0f);
                    C2D_DrawLine(x0, y0, col, x1, y1, col, 1.5f, 0.5f);
                }
                break;
            default:
                C2D_DrawLine(c->x - 3, c->y, col, c->x + 3, c->y, col, 1.5f, 0.5f);
                C2D_DrawLine(c->x, c->y - 3, col, c->x, c->y + 3, col, 1.5f, 0.5f);
                break;
        }
    }
}

static float slider_x(int i) { return SLIDER_X0 + i * (SLIDER_W + SLIDER_GAP); }

static void draw_slider(const AppState *app, int i) {
    const float x = slider_x(i), v = app->params[i];
    const u32 rgb = vw_param_rgb(i);
    const bool active = app->active_slider == i;

    ui_text_fx(x + SLIDER_W / 2.0f, 9, 0.5f, vw(rgb, 0xFF), vw(VW_INK, 0xFF), C2D_AlignCenter, "%s",
               engine_param_name((EngineParam)i));

    ui_bevel(x, SLIDER_Y0, SLIDER_W, SLIDER_H, vw(VW_INK, 0xFF), true);
    const float ix = x + 3, iw = SLIDER_W - 6, ih = SLIDER_H - 6, iy = SLIDER_Y0 + 3;
    const float fill = v * ih;
    if (fill > 0.5f) {
        C2D_DrawRectangle(ix, iy + ih - fill, 0.5f, iw, fill, vw(rgb, 0xFF), vw(rgb, 0xFF),
                          vw_mix(rgb, VW_PURPLE, 0.7f, 0xFF), vw_mix(rgb, VW_PURPLE, 0.7f, 0xFF));
        // CRT scanlines.
        for (float y = iy + ih - fill; y < iy + ih; y += 4.0f)
            C2D_DrawRectSolid(ix, y, 0.5f, iw, 1, vw(VW_INK, 0x40));
    }
    // Level line and a triangle pointer in the gutter.
    float ly = iy + ih - fill;
    C2D_DrawRectSolid(ix, ly - 1, 0.5f, iw, 2, vw(0xFFFFFF, 0xFF));
    float px = x + SLIDER_W + 2, r = active ? 7.0f : 5.0f;
    C2D_DrawTriangle(px, ly, vw(0xFFFFFF, 0xFF), px + r * 1.4f, ly - r, vw(rgb, 0xFF), px + r * 1.4f, ly + r,
                     vw(VW_PINK, 0xFF), 0.5f);

    ui_text(x + SLIDER_W / 2.0f, SLIDER_Y0 + SLIDER_H + 3, 0.45f, vw(active ? 0xFFFFFF : VW_LILAC, 0xFF),
            C2D_AlignCenter, "%d", (int)(v * 100.0f + 0.5f));
}

static u32 key_rgb(const AppState *app, int key) {
    if (app->mode == PLAY_DRUM) return VW_MINT;
    return key < 4 ? VW_CYAN : VW_PINK;
}

static void draw_key(const AppState *app, int k) {
    const float x = (float)(k * KEY_W), y = KEYS_Y0;
    const bool lit = app->key_lit[k];
    const u32 rgb = key_rgb(app, k);
    if (lit) {
        ui_bevel(x + 1, y, KEY_W - 2, KEYS_H, vw(rgb, 0xFF), true);
    } else {
        ui_bevel(x + 1, y, KEY_W - 2, KEYS_H, vw(VW_DUSK, 0xFF), false);
        // A sliver of the key's colour along the top, like a keycap stripe.
        C2D_DrawRectSolid(x + 3, y + 2, 0.5f, KEY_W - 6, 3, vw(rgb, 0xFF));
    }
    u32 ink = lit ? vw(VW_INK, 0xFF) : vw(VW_LILAC, 0xFF);
    ui_text(x + KEY_W / 2.0f, y + 14, app->mode == PLAY_DRUM ? 0.36f : 0.55f, ink, C2D_AlignCenter, "%s",
            music_key_label(k, app->mode));
    ui_text(x + KEY_W / 2.0f, y + 44, 0.42f, lit ? ink : vw(rgb, 0xC0), C2D_AlignCenter, "%s", kButtonHints[k]);
}

void ui_draw_bottom(const AppState *app) {
    C2D_DrawRectangle(0, 0, 0.5f, BOTTOM_W, BOTTOM_H, vw(VW_NIGHT, 0xFF), vw(VW_NIGHT, 0xFF), vw(VW_DUSK, 0xFF),
                      vw(VW_DUSK, 0xFF));
    draw_confetti();
    for (int i = 0; i < PARAM_COUNT; i++) draw_slider(app, i);
    for (int k = 0; k < MUSIC_KEYS; k++) draw_key(app, k);
}
