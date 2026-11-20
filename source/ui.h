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

// Bottom screen, every page: a row of six icon tabs at the top...
#define TAB_Y 2
#define TAB_H 18
#define TAB_X0 2
#define TAB_W 20
#define TAB_PITCH 22
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
#define SLIDER_H 54
// ...then the KEY row (root, scale) and the ARP row (on, mode, rate, octaves).
#define KEYROW_Y 124
#define ARPROW_Y 144
#define ROW_H 16
#define ROW_X0 8
#define ROOT_W 54     // KEY: tap the left half to go down a semitone, the right half up
#define SCALE_X 66
#define SCALE_W 174   // the scale: left half back, right half on
#define ARP_W 54      // ARP stickers: on/off, mode, rate, octaves
#define ARP_PITCH 58
typedef enum {
    SND_HIT_NONE = -1,
    SND_HIT_ROOT_DOWN,
    SND_HIT_ROOT_UP,
    SND_HIT_SCALE_PREV,
    SND_HIT_SCALE_NEXT,
    SND_HIT_ARP_ON,
    SND_HIT_ARP_MODE,
    SND_HIT_ARP_RATE,
    SND_HIT_ARP_OCT,
} SoundRowHit;
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
#define SEQ_SLOT_W 18
#define SEQ_SLOT_PITCH 20
#define SEQ_SONG_X 166
#define SEQ_SONG_W 30
#define SEQ_ROLL_X 199
#define SEQ_ROLL_W 30
#define SEQ_BAR_X 232       // BAR: which bar is on screen (tap) / the length (hold)
#define SEQ_BAR_W 36
#define SEQ_SWING_X 271
#define SEQ_SWING_W 45
#define SEQ_HIT_SONG SEQ_SLOTS        // ui_hit_seq_slot results beyond 0..7
#define SEQ_HIT_SWING (SEQ_SLOTS + 1)
#define SEQ_HIT_ROLL (SEQ_SLOTS + 2)
#define SEQ_HIT_BAR (SEQ_SLOTS + 3)

// SEQ page, ROLL view: a scale/chromatic toggle and a keyboard strip on the
// left, then 16 step columns by ROLL_ROWS pitch rows.
#define ROLL_TOGGLE_X 4
#define ROLL_TOGGLE_Y 26
#define ROLL_TOGGLE_W 20
#define ROLL_TOGGLE_H 11
#define ROLL_KEYS_X 4
#define ROLL_KEYS_W 20
#define ROLL_X0 26
#define ROLL_CELL_W 18
#define ROLL_Y0 38
#define ROLL_ROW_H 8
#define ROLL_ROWS 13
#define ROLL_PART_X0 28   // part pickers in the roll's header
#define ROLL_PART_W 16
#define ROLL_PART_PITCH 18

// SEQ page, SONG sheet (hold SONG): the arrangement as stickers, 8 x 4.
#define ARR_HEAD_Y 26
#define ARR_HEAD_H 15
#define ARR_END_X 4
#define ARR_END_W 56
#define ARR_MUTE_X0 160
#define ARR_MUTE_W 22
#define ARR_MUTE_PITCH 24
#define ARR_DEL_X 258
#define ARR_DEL_W 34
#define ARR_X0 4
#define ARR_Y0 45
#define ARR_COLS 8
#define ARR_CELL_W 36
#define ARR_CELL_H 21
#define ARR_PITCH_X 39
#define ARR_PITCH_Y 24
typedef enum {
    ARR_HIT_NONE = -1,
    ARR_HIT_END,
    ARR_HIT_DEL,
    ARR_HIT_MUTE,                // + part (PARTS = drums)
    ARR_HIT_CELL = ARR_HIT_MUTE + PARTS + 1,  // + row
} ArrHit;

// SYNTH page: part stickers in the tab row (tap: pick, hold: mute).
#define PART_TAB_X0 136
#define PART_TAB_W 58
#define PART_TAB_PITCH 60

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
// SYNTH page, MIC sound: the sample editor in place of the creature pad.
// CHOP and LIB stickers beside REC; the take with START / END handles; DECAY
// and TONE sliders underneath.
#define SMP_CHOP_X 160
#define SMP_CHOP_W 42
#define SMP_LIB_X 206
#define SMP_LIB_W 42
#define SMP_WAVE_X 118
#define SMP_WAVE_Y 50
#define SMP_WAVE_W 190
#define SMP_WAVE_H 70
#define SMP_GRIP_H 12      // handle grips under the wave
#define SMP_SLIDER_Y 138
#define SMP_SLIDER_H 15
#define SMP_SLIDER_X0 116
#define SMP_SLIDER_W 94
#define SMP_SLIDER_PITCH 100
typedef enum {
    SMP_HIT_NONE = -1,
    SMP_HIT_CHOP,
    SMP_HIT_LIB,
    SMP_HIT_WAVE,      // the take (and the grips under it)
    SMP_HIT_SLIDER,    // + macro
} SmpHit;
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
// FX automation: 16 step cells along the bottom of the pad (tap one: erase it).
#define FX_AUTO_Y (FX_PAD_Y + FX_PAD_H - 11)
#define FX_AUTO_H 7
#define FX_AUTO_X0 (FX_PAD_X + 8)
#define FX_AUTO_PITCH ((FX_PAD_W - 16) / 16.0f)

// TAPE page: track stickers (tap: arm, M: mute, hold: wipe) and SEQ LINK on
// the left; the scrolling 4-lane tape window (drag to scratch), transport
// and the speed strip on the right.
#define TAPE_TRK_X 4
#define TAPE_TRK_W 74
#define TAPE_TRK_Y0 26
#define TAPE_TRK_H 24
#define TAPE_TRK_PITCH 27
#define TAPE_MUTE_W 20
#define TAPE_SRC_DX 15   // source chip (ALL / DRUMS / PART n), from the sticker's left
#define TAPE_SRC_W 37
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
#define TAPE_TIME_W 84
#define TAPE_SMP_X 278      // SAMPLE: the bar under the playhead -> the MIC sound
#define TAPE_SMP_W 38
#define TAPE_SPEED_Y 136
#define TAPE_SPEED_H 22

// MIX page: four channel strips (parts 1-3, drums). Each: a name sticker
// (tap: play that part), a fader with its meter inside, four knobs in two
// rows (DRIVE CRUSH / TONE PAN; drag up and down), then MUTE and SOLO.
#define MIX_X0 4
#define MIX_W 75
#define MIX_PITCH 79
#define MIX_HEAD_Y 25
#define MIX_HEAD_H 17
#define MIX_FADER_DX 3      // from the strip's left edge
#define MIX_FADER_W 24
#define MIX_FADER_Y 47
#define MIX_FADER_H 90
#define MIX_KNOB_DX0 42     // knob centres, from the strip's left edge
#define MIX_KNOB_DX1 65
#define MIX_KNOB_Y0 58      // three rows: DRV CRSH / ECHO VERB / TONE PAN
#define MIX_KNOB_PITCH_Y 30
// Where each MixKnob sits: its column (0, 1) and row (0..2).
static inline int mix_knob_col(int knob) { return knob & 1; }
static inline int mix_knob_row(int knob) { return knob < 2 ? 0 : knob < 4 ? 2 : 1; }
static inline int mix_knob_at(int row, int col) { return row == 0 ? col : row == 1 ? 4 + col : 2 + col; }
// (Plain expressions, so the story build's script can use them as constants.)
#define MIX_KNOB_X(knob) (((knob) & 1) ? MIX_KNOB_DX1 : MIX_KNOB_DX0)
#define MIX_KNOB_Y(knob) (MIX_KNOB_Y0 + ((knob) < 2 ? 0 : (knob) < 4 ? 2 : 1) * MIX_KNOB_PITCH_Y)
#define MIX_KNOB_R 8
#define MIX_BTN_Y 142
#define MIX_BTN_H 19
#define MIX_MUTE_DX 3
#define MIX_SOLO_DX 39
#define MIX_BTN_W 33
#define MIX_KNOB_DRAG 90.0f  // pixels of drag for the whole knob range
#define MIXDOWN_X 222        // MIXDOWN sticker, in the tab row of the MIX page
#define MIXDOWN_W 94
typedef enum {
    MIX_HIT_NONE = -1,
    MIX_HIT_HEAD,
    MIX_HIT_FADER,
    MIX_HIT_MUTE,
    MIX_HIT_SOLO,
    MIX_HIT_KNOB,  // + knob
} MixHit;

// SOUND page: SONGS button under THEME.
#define SONGS_BTN_X THEME_X
#define SONGS_BTN_Y 136
#define SONGS_BTN_W THEME_W
#define SONGS_BTN_H 22
// SONGS page: the list (with scroll stickers) and a column of buttons.
#define SONG_LIST_X 4
#define SONG_LIST_W 196
#define SONG_LIST_Y0 26
#define SONG_ROW_H 22
#define SONG_ROW_PITCH 26
#define SONG_ROWS 5
#define SONG_SCROLL_X 204
#define SONG_SCROLL_W 22
#define SONG_SCROLL_H 62
#define SONG_BTN_X 232
#define SONG_BTN_W 84
#define SONG_BTN_Y0 26
#define SONG_BTN_H 24
#define SONG_BTN_PITCH 27
typedef enum {
    SONG_HIT_NONE = -1,
    SONG_HIT_SAVE_AS,
    SONG_HIT_SAVE,
    SONG_HIT_LOAD,
    SONG_HIT_DELETE,
    SONG_HIT_NEW,
    SONG_HIT_UP,
    SONG_HIT_DOWN,
    SONG_HIT_CONFIRM,  // the "replace?" card while it is up
    SONG_HIT_ROW,      // + visible row index
} SongHit;
#define SONG_CONFIRM_X 10
#define SONG_CONFIRM_Y 44
#define SONG_CONFIRM_W 184
#define SONG_CONFIRM_H 70

typedef enum {
    TAPE_HIT_NONE = -1,
    TAPE_HIT_REW,
    TAPE_HIT_PLAY,
    TAPE_HIT_REC,
    TAPE_HIT_LINK,
    TAPE_HIT_WINDOW,
    TAPE_HIT_SPEED,
    TAPE_HIT_SAMPLE,
    TAPE_HIT_TRACK,  // + track index
    TAPE_HIT_MUTE = TAPE_HIT_TRACK + TAPE_TRACKS,  // + track index
    TAPE_HIT_SOURCE = TAPE_HIT_MUTE + TAPE_TRACKS,  // + track index
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
int ui_hit_sound_rows(int x, int y);  // the SOUND page's KEY / ARP rows: a SoundRowHit
int ui_hit_tab(int x, int y);  // a Page, or -1
SeqButton ui_hit_seq_button(int x, int y);
// row 0 is the synth row, rows 1..8 are drum lanes 0..7, SEQ_ROW_ACCENT accents.
bool ui_hit_seq_cell(int x, int y, int *step, int *row);
int ui_hit_seq_slot(int x, int y);  // 0..7, SEQ_HIT_SONG, SEQ_HIT_SWING or -1
float ui_swing_at(int x);           // swing strip position -> 0..SEQ_SWING_MAX
bool ui_hit_roll_toggle(int x, int y);
int ui_hit_roll_part(int x, int y);     // part 0..PARTS-1, or -1
int ui_hit_part_tab(int x, int y);      // SYNTH page part sticker, or -1
bool ui_hit_seq_part_chip(int x, int y);  // the grid's synth-row chip
bool ui_hit_roll_keys(int x, int y);
int ui_hit_arr(int x, int y);  // SONG sheet: an ArrHit
// Roll cell under (x, y): step 0..15 and row 0..ROLL_ROWS-1 counted up from
// the bottom. The clamped form always answers (for drags off the edge).
bool ui_roll_cell(int x, int y, int *step, int *row);
void ui_roll_cell_clamped(int x, int y, int *step, int *row);
int ui_hit_sound_tile(int x, int y);
bool ui_in_synth_pad(int x, int y);
bool ui_hit_synth_dice(int x, int y);
bool ui_hit_synth_rec(int x, int y);  // the MIC sound's REC sticker
int ui_hit_sampler(int x, int y);       // the rest of the MIC editor: an SmpHit
float ui_sampler_pos(int x);            // position along the take, 0..1
float ui_sampler_slider(int x, int m);  // slider m's value for a touch at x
int ui_hit_bank_cell(int x, int y);  // 0..127, or -1
int ui_hit_fx_sticker(int x, int y);  // an FxKind, FX_HIT_HOLD, or -1
bool ui_in_fx_pad(int x, int y);
int ui_hit_fx_auto(int x, int y);  // an automation step cell, or -1
bool ui_hit_songs_button(int x, int y);  // on the SOUND page
// MIX page: a MixHit (knobs offset by MixKnob), and the strip it is on.
int ui_hit_mix(int x, int y, int *strip);
bool ui_hit_mixdown(int x, int y);
int ui_hit_songs(int x, int y, bool confirming);  // a SongHit (rows offset by index)
int ui_hit_tape(int x, int y);  // a TapeHit (tracks / mutes offset by index)
float ui_tape_speed_at(int x);  // speed strip position -> -2..2 (snaps to -1, 0, 1)
// The TAPE page shows REC armed and the speed knob from these.
void ui_tape_ui_state(bool armed, float speed);
// Normalised position on a pad (clamped; y is 0 at the bottom, 1 at the top).
void ui_synth_pad_pos(int x, int y, float *nx, float *ny);
void ui_fx_pad_pos(int x, int y, float *nx, float *ny);
// Slider value (0..1) for a touch at height y.
float ui_slider_value(int y);
