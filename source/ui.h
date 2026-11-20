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
#define TAB_W 24
#define TAB_PITCH 26
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
#define SEQ_PLAY_X 134
#define SEQ_REC_X 162
#define SEQ_BTN_W 26
#define SEQ_BPM_DOWN_X 190
#define SEQ_BPM_VALUE_X 206
#define SEQ_BPM_VALUE_W 30
#define SEQ_BPM_UP_X 238
#define SEQ_BPM_BTN_W 14
#define SEQ_CLEAR_X 256
#define SEQ_CLEAR_W 28
#define SEQ_DICE_X 288
#define SEQ_DICE_W 28
#define SEQ_GRID_X 22
#define SEQ_CELL_W 18
#define SEQ_SYNTH_Y 27
#define SEQ_SYNTH_H 14
#define SEQ_ACC_Y 44     // thin accent row between the synth row and the drums
#define SEQ_ACC_H 7
#define SEQ_DRUM_Y 53
#define SEQ_DRUM_PITCH 11
#define SEQ_DRUM_H 10
#define SEQ_ROW_ACCENT (1 + SEQ_LANES)  // ui_hit_seq_cell row for the accent row
// Pattern slots A..H, SONG and SWING along the bottom of the SEQ page.
#define SEQ_SLOT_Y 145
#define SEQ_SLOT_H 17
#define SEQ_SLOT_X0 4
#define SEQ_SLOT_W 21
#define SEQ_SLOT_PITCH 23
#define SEQ_SONG_X 190
#define SEQ_SONG_W 44
#define SEQ_SWING_X 238
#define SEQ_SWING_W 78
#define SEQ_HIT_SONG SEQ_SLOTS        // ui_hit_seq_slot results beyond 0..7
#define SEQ_HIT_SWING (SEQ_SLOTS + 1)

// SYNTH page: a 2x5 grid of sound tiles, and the pad (an XY creature pad
// for CUSTOM sounds, a 16x8 patch-dot grid for BANK sounds).
#define SYN_TILE_COLS 2
#define SYN_TILE_X0 4
#define SYN_TILE_Y0 26
#define SYN_TILE_W 49
#define SYN_TILE_H 24
#define SYN_TILE_PITCH_X 52
#define SYN_TILE_PITCH_Y 26
#define SYN_PAD_X 110
#define SYN_PAD_Y 26
#define SYN_PAD_W 206
#define SYN_PAD_H 132
#define SYN_DICE_X (SYN_PAD_X + SYN_PAD_W - 22)
#define SYN_DICE_Y (SYN_PAD_Y + 3)
#define SYN_DICE_W 18
#define SYN_REC_X (SYN_DICE_X - 42)
#define SYN_REC_W 38
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

// TAPE page: track stickers (tap: arm, M: mute, hold: wipe) and SEQ LINK on
// the left; the scrolling 4-lane tape window (drag to scratch), transport
// and the speed strip on the right.
#define TAPE_TRK_X 4
#define TAPE_TRK_W 74
#define TAPE_TRK_Y0 26
#define TAPE_TRK_H 24
#define TAPE_TRK_PITCH 27
#define TAPE_MUTE_W 20
#define TAPE_LINK_Y 136
#define TAPE_LINK_H 22
#define TAPE_WIN_X 82
#define TAPE_WIN_Y 26
#define TAPE_WIN_W 234
#define TAPE_WIN_H 80
#define TAPE_WIN_SECONDS 8.0f  // seconds of tape visible in the window
#define TAPE_BTN_Y 110
#define TAPE_BTN_H 22
#define TAPE_REW_X 82
#define TAPE_PLAY_X 118
#define TAPE_REC_X 154
#define TAPE_BTN_W 32
#define TAPE_TIME_X 190
#define TAPE_TIME_W 126
#define TAPE_SPEED_Y 136
#define TAPE_SPEED_H 22

typedef enum {
    TAPE_HIT_NONE = -1,
    TAPE_HIT_REW,
    TAPE_HIT_PLAY,
    TAPE_HIT_REC,
    TAPE_HIT_LINK,
    TAPE_HIT_WINDOW,
    TAPE_HIT_SPEED,
    TAPE_HIT_TRACK,  // + track index
    TAPE_HIT_MUTE = TAPE_HIT_TRACK + TAPE_TRACKS,  // + track index
} TapeHit;

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
// row 0 is the synth row, rows 1..8 are drum lanes 0..7, SEQ_ROW_ACCENT accents.
bool ui_hit_seq_cell(int x, int y, int *step, int *row);
int ui_hit_seq_slot(int x, int y);  // 0..7, SEQ_HIT_SONG, SEQ_HIT_SWING or -1
float ui_swing_at(int x);           // swing strip position -> 0..SEQ_SWING_MAX
int ui_hit_sound_tile(int x, int y);
bool ui_in_synth_pad(int x, int y);
bool ui_hit_synth_dice(int x, int y);
bool ui_hit_synth_rec(int x, int y);  // the MIC sound's REC sticker
int ui_hit_bank_cell(int x, int y);  // 0..127, or -1
int ui_hit_fx_sticker(int x, int y);  // an FxKind, FX_HIT_HOLD, or -1
bool ui_in_fx_pad(int x, int y);
int ui_hit_tape(int x, int y);  // a TapeHit (tracks / mutes offset by index)
float ui_tape_speed_at(int x);  // speed strip position -> -2..2 (snaps to -1, 0, 1)
// The TAPE page shows REC armed and the speed knob from these.
void ui_tape_ui_state(bool armed, float speed);
// Normalised position on a pad (clamped; y is 0 at the bottom, 1 at the top).
void ui_synth_pad_pos(int x, int y, float *nx, float *ny);
void ui_fx_pad_pos(int x, int y, float *nx, float *ny);
// Slider value (0..1) for a touch at height y.
float ui_slider_value(int y);
