// ui.h -- citro2d drawing for both screens, plus the bottom-screen layout
// that touch input hit-tests against.
//
// Look: loud 90s Memphis. Switchable themes (see theme.c), layered
// see-through triangles with real stereoscopic depth on the top screen,
// pattern backgrounds and sticker-style cards with hard shadows below.
// Every note fires a burst of motifs.
#pragma once

#include <citro2d.h>

#include "app.h"
#include "audio_out.h"

#define TOP_W 400
#define TOP_H 240
#define BOTTOM_W 320
#define BOTTOM_H 240

// Bottom screen: four vertical sliders...
#define SLIDER_X0 8
#define SLIDER_W 46
#define SLIDER_GAP 16
#define SLIDER_Y0 30
#define SLIDER_H 112
// ...a theme button to their right...
#define THEME_X 262
#define THEME_Y 30
#define THEME_W 52
#define THEME_H 74
// ...above an eight-key strip.
#define KEYS_Y0 166
#define KEYS_H 66
#define KEY_W (BOTTOM_W / MUSIC_KEYS)

void ui_init(void);
void ui_exit(void);

// Switch colour theme (wraps around). Rebuilds the patterns.
void ui_set_theme(int index);
int ui_theme_count(void);

// Once per frame, before drawing: clears text, advances animation.
void ui_update(const AppState *app, const AudioStats *stats);

// Draw the top screen for one eye. eye is -1 (left) or +1 (right), already
// scaled by the 3D slider (0 = flat). Call once per eye.
void ui_draw_top(const AppState *app, const AudioStats *stats, const int16_t *scope, float eye);
void ui_draw_bottom(const AppState *app);

// Tell the visuals a key was struck (spawns a burst of motifs).
void ui_key_struck(int key, PlayMode mode);

// Hit tests in bottom-screen pixels. Return -1 / false when nothing is hit.
int ui_hit_slider(int x, int y);
int ui_hit_key(int x, int y);
bool ui_hit_theme(int x, int y);
// Slider value (0..1) for a touch at height y.
float ui_slider_value(int y);
