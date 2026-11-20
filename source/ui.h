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

// Bottom screen, both pages: a tab row at the top...
#define TAB_Y 2
#define TAB_H 18
#define TAB_SOUND_X 4
#define TAB_SOUND_W 56
#define TAB_SEQ_X 64
#define TAB_SEQ_W 44
// ...and the eight-key strip at the bottom.
#define KEYS_Y0 166
#define KEYS_H 66
#define KEY_W (BOTTOM_W / MUSIC_KEYS)

// SOUND page: four vertical sliders and a theme button.
#define SLIDER_X0 8
#define SLIDER_W 46
#define SLIDER_GAP 16
#define SLIDER_LABEL_Y 26
#define SLIDER_Y0 46
#define SLIDER_H 94
#define THEME_X 262
#define THEME_Y 46
#define THEME_W 52
#define THEME_H 62

// SEQ page: transport stickers in the tab row, then the step grid
// (one synth row above eight drum lanes).
#define SEQ_PLAY_X 114
#define SEQ_REC_X 152
#define SEQ_BTN_W 34
#define SEQ_BPM_DOWN_X 190
#define SEQ_BPM_VALUE_X 210
#define SEQ_BPM_UP_X 254
#define SEQ_BPM_BTN_W 18
#define SEQ_CLEAR_X 278
#define SEQ_CLEAR_W 38
#define SEQ_GRID_X 22
#define SEQ_CELL_W 18
#define SEQ_SYNTH_Y 27
#define SEQ_SYNTH_H 16
#define SEQ_DRUM_Y 48
#define SEQ_DRUM_PITCH 13
#define SEQ_DRUM_H 12

typedef enum {
    SEQ_BTN_NONE = -1,
    SEQ_BTN_PLAY,
    SEQ_BTN_REC,
    SEQ_BTN_BPM_DOWN,
    SEQ_BTN_BPM_UP,
    SEQ_BTN_CLEAR,
} SeqButton;

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
// A smaller burst for a hit fired by the sequencer.
void ui_seq_struck(int key, bool drum);

// Hit tests in bottom-screen pixels. Return -1 / false when nothing is hit.
int ui_hit_slider(int x, int y);
int ui_hit_key(int x, int y);
bool ui_hit_theme(int x, int y);
int ui_hit_tab(int x, int y);  // a Page, or -1
SeqButton ui_hit_seq_button(int x, int y);
// row 0 is the synth row, rows 1..8 are drum lanes 0..7.
bool ui_hit_seq_cell(int x, int y, int *step, int *row);
// Slider value (0..1) for a touch at height y.
float ui_slider_value(int y);
