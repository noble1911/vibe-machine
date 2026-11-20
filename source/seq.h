// seq.h -- the pattern sequencer: 16 steps, 8 pattern slots, song mode,
// swing, accents, drums on a grid and melodies as a piano roll.
//
// The clock runs on the audio thread: audio_out.c calls seq_advance() before
// rendering every block, so steps follow the sample clock rather than the
// 60 Hz screen. Everything else is called from the UI thread; a lock keeps
// the two sides consistent. Notes leave through a callback (the engine on
// the 3DS, a recorder in tests/), so this file also builds on a PC.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "music.h"

#define SEQ_STEPS 16
#define SEQ_LANES MUSIC_KEYS  // one drum lane per key (kick, snare, hat...)
#define SEQ_SLOTS 8           // patterns A..H
#define SEQ_NOTES 64          // piano-roll notes per pattern
#define SEQ_BPM_MIN 40
#define SEQ_BPM_MAX 240
#define SEQ_SWING_MAX 0.5f    // even steps up to 50% longer, odd ones shorter
#define SEQ_GATE 0.55f        // a note sounds this far into its last step

// Velocities: accented steps hit harder.
#define SEQ_VEL_DRUM 0.72f
#define SEQ_VEL_DRUM_ACCENT 1.0f
#define SEQ_VEL_SYNTH 0.62f
#define SEQ_VEL_SYNTH_ACCENT 0.9f

// What a key press plays (a note, or a chord of up to 3): used for painting
// the grid's synth row and for live recording.
typedef struct {
    int8_t key;     // which key played it (for visuals), -1 if none
    uint8_t count;  // 0 = rest
    uint8_t notes[MUSIC_MAX_NOTES];
} SeqNote;

// One note on the piano roll.
typedef struct {
    uint8_t step;   // 0..15, where it starts
    uint8_t pitch;  // MIDI note
    uint8_t len;    // 1..16 steps (may run past the end of the bar)
    uint8_t vel;    // 1..255 (velocity = vel / 255)
    uint8_t part;   // which instrument part plays it (0..PARTS-1)
} SeqRollNote;

typedef struct {
    uint8_t drums[SEQ_STEPS];  // bit n = drum lane n hits on this step
    uint16_t accents;          // bit s = step s is accented
    uint8_t count;             // notes in use
    SeqRollNote notes[SEQ_NOTES];
} SeqPattern;

// The song's arrangement (SONG mode): rows played in order, each a slot
// repeated `reps` times with some parts dropped out. Empty: SONG chains
// the slots that have something in them, as before.
#define SEQ_ARR_ROWS 32
#define SEQ_ARR_REPS_MAX 8
typedef struct {
    uint8_t slot;   // 0..SEQ_SLOTS-1
    uint8_t reps;   // 1..SEQ_ARR_REPS_MAX bars
    uint8_t mutes;  // bit n = part n silent in this row; bit PARTS = the drums
} SeqArrRow;
typedef struct {
    uint8_t count;
    bool loop;      // at the end: start again (true) or stop (false)
    SeqArrRow rows[SEQ_ARR_ROWS];
} SeqArrangement;

typedef struct {
    bool playing;
    bool recording;
    int bpm;
    int step;        // step most recently fired (valid while playing)
    uint32_t fired;  // increments every time a step fires
    int slot;        // pattern playing / being edited (0..7 = A..H)
    int queued;      // slot that takes over at the next bar, or -1
    bool song;       // chain the non-empty slots in order
    float swing;     // 0..SEQ_SWING_MAX
    uint8_t used;    // bit n = slot n has something in it
    uint8_t muted;   // bit n = part n is muted; bit PARTS = the drums
    int arr_row;     // arrangement row playing (SONG mode with an arrangement), else -1
    int arr_rep;     // which repeat of it (0-based)
    uint32_t ends;   // bumps each time an arrangement plays to its end and stops
} SeqStatus;

// velocity 0 means note-off. `synth` is SYNTH_DRUMS or part_synth(part).
typedef void (*SeqNoteFn)(int synth, int note, float velocity);

void seq_init(SeqNoteFn out);

// --- audio thread --------------------------------------------------------
// Advance the clock by one block of `frames` at `rate` Hz and send any
// notes that fall due.
void seq_advance(int frames, int rate);

// --- UI thread -----------------------------------------------------------
void seq_toggle_play(void);
void seq_set_recording(bool on);
void seq_set_bpm(int bpm);
void seq_set_swing(float swing);
void seq_set_song(bool on);
void seq_set_arrangement(const SeqArrangement *a);
void seq_get_arrangement(SeqArrangement *out);
void seq_set_part_mute(int part, bool muted);  // part PARTS = the drums
void seq_clear(void);                       // the current slot
void seq_reset(void);  // a new song: empty slots, starter groove in A, no swing / song
void seq_set_pattern(const SeqPattern *p);  // the current slot

// Switch pattern: immediately when stopped, at the next bar while playing.
void seq_select_slot(int slot);
// Copy the current pattern into `slot` and switch to it.
void seq_copy_to_slot(int slot);

void seq_set_drum(int step, int lane, bool on);
void seq_set_accent(int step, bool on);
// The grid's synth row for one part: replace whatever that part starts on
// `step` with `note` (one step long), or clear it (note NULL or count 0).
void seq_set_synth(int part, int step, const SeqNote *note);

// Piano roll (current slot). Indices are positions in SeqPattern.notes and
// stay valid until a note is removed.
int seq_roll_add(int part, int step, int pitch, int len, int vel);  // index, or -1 if full
void seq_roll_set(int index, int step, int pitch, int len);
void seq_roll_remove(int index);
void seq_roll_set_vel(int index, int vel);  // 1..255
// The part's note covering (step, pitch), i.e. starting at or before step
// and still sounding there; -1 if none.
int seq_roll_find(int part, int step, int pitch);
// Replace one part's notes in the current slot with those in `p` (whose
// notes are taken as that part's); drums and accents come from `p` too.
void seq_set_part_pattern(int part, const SeqPattern *p);

// Live recording (only acts while playing and recording): the hit lands on
// the nearest step. Synth notes are as long as you hold them: call
// seq_record_release() with the same note when the key comes up.
void seq_record_drum(int lane);
void seq_record_synth(int part, const SeqNote *note);
void seq_record_release(int part, const SeqNote *note);

SeqStatus seq_status(void);
void seq_snapshot(SeqPattern *out);  // the current slot

// Whole-bank access for saving and loading.
void seq_get_slot(int slot, SeqPattern *out);
void seq_put_slot(int slot, const SeqPattern *p);

// Notes of `part` (or any part, if -1) in `p` that start on `step`.
int seq_notes_at(const SeqPattern *p, int part, int step, int *pitches, int max);
