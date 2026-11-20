// ui.h -- citro2d drawing for both screens, plus the bottom-screen layout
// that touch input hit-tests against.
#pragma once

#include <citro2d.h>

#include "app.h"
#include "audio_out.h"

#define TOP_W 400
#define TOP_H 240
#define BOTTOM_W 320
#define BOTTOM_H 240

// Bottom screen: four vertical sliders...
#define SLIDER_X0 16
#define SLIDER_W 56
#define SLIDER_GAP 20
#define SLIDER_Y0 34
#define SLIDER_H 110
// ...above an eight-key strip.
#define KEYS_Y0 168
#define KEYS_H 68
#define KEY_W (BOTTOM_W / MUSIC_KEYS)

void ui_init(void);
void ui_exit(void);

// Call once per frame before drawing either screen.
void ui_begin_frame(void);
void ui_draw_top(const AppState *app, const AudioStats *stats, const int16_t *scope);
void ui_draw_bottom(const AppState *app);

// Hit tests in bottom-screen pixels. Return -1 when nothing is hit.
int ui_hit_slider(int x, int y);
int ui_hit_key(int x, int y);
// Slider value (0..1) for a touch at height y.
float ui_slider_value(int y);
