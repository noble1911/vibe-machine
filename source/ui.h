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

// Bottom screen, every page: a row of four icon tabs at the top...
#define TAB_Y 2
#define TAB_H 18
#define TAB_X0 2
#define TAB_W 28
#define TAB_PITCH 30
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
#define SEQ_PLAY_X 126
#define SEQ_REC_X 160
#define SEQ_BTN_W 30
#define SEQ_BPM_DOWN_X 194
#define SEQ_BPM_VALUE_X 212
#define SEQ_BPM_VALUE_W 34
#define SEQ_BPM_UP_X 248
#define SEQ_BPM_BTN_W 16
#define SEQ_CLEAR_X 270
#define SEQ_CLEAR_W 22
#define SEQ_DICE_X 294
#define SEQ_DICE_W 22
#define SEQ_GRID_X 22
#define SEQ_CELL_W 18
#define SEQ_SYNTH_Y 27
#define SEQ_SYNTH_H 16
#define SEQ_DRUM_Y 48
#define SEQ_DRUM_PITCH 13
#define SEQ_DRUM_H 12

// SYNTH page: a 3x3 grid of sound tiles, and the pad (an XY creature pad
// for CUSTOM sounds, a 16x8 patch-dot grid for BANK sounds).
#define SYN_TILE_X0 4
#define SYN_TILE_Y0 26
#define SYN_TILE_W 32
#define SYN_TILE_H 42
#define SYN_TILE_PITCH_X 34
#define SYN_TILE_PITCH_Y 44
#define SYN_PAD_X 110
#define SYN_PAD_Y 26
#define SYN_PAD_W 206
#define SYN_PAD_H 132
#define SYN_DICE_X (SYN_PAD_X + SYN_PAD_W - 22)
#define SYN_DICE_Y (SYN_PAD_Y + 3)
#define SYN_DICE_W 18
#define BANK_COLS 16
#define BANK_ROWS 8
#define BANK_X0 (SYN_PAD_X + 6)
#define BANK_Y0 (SYN_PAD_Y + 18)
#define BANK_CELL_W 12
#define BANK_CELL_H 13

// FX page: four effect stickers and HOLD on the left, the pad on the right.
#define FX_STICK_X 4
#define FX_STICK_W 72
#define FX_STICK_Y0 26
#define FX_STICK_H 24
#define FX_STICK_PITCH 27
#define FX_HOLD_Y 136
#define FX_HOLD_H 22
#define FX_PAD_X 82
#define FX_PAD_Y 26
#define FX_PAD_W 234
#define FX_PAD_H 132
#define FX_HIT_HOLD FX_COUNT  // ui_hit_fx_sticker() result for HOLD

typedef enum {
    SEQ_BTN_NONE = -1,
    SEQ_BTN_PLAY,
    SEQ_BTN_REC,
    SEQ_BTN_BPM_DOWN,
    SEQ_BTN_BPM_UP,
    SEQ_BTN_CLEAR,
    SEQ_BTN_DICE,
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
int ui_hit_sound_tile(int x, int y);
bool ui_in_synth_pad(int x, int y);
bool ui_hit_synth_dice(int x, int y);
int ui_hit_bank_cell(int x, int y);  // 0..127, or -1
int ui_hit_fx_sticker(int x, int y);  // an FxKind, FX_HIT_HOLD, or -1
bool ui_in_fx_pad(int x, int y);
// Normalised position on a pad (clamped; y is 0 at the bottom, 1 at the top).
void ui_synth_pad_pos(int x, int y, float *nx, float *ny);
void ui_fx_pad_pos(int x, int y, float *nx, float *ny);
// Slider value (0..1) for a touch at height y.
float ui_slider_value(int y);
