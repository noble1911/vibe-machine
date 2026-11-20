// seq.h -- the 16-step pattern sequencer, with 8 pattern slots, song mode,
// swing and accents.
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
#define SEQ_BPM_MIN 40
#define SEQ_BPM_MAX 240
#define SEQ_SWING_MAX 0.5f    // even steps up to 50% longer, odd ones shorter

// Velocities: accented steps hit harder.
#define SEQ_VEL_DRUM 0.72f
#define SEQ_VEL_DRUM_ACCENT 1.0f
#define SEQ_VEL_SYNTH 0.62f
#define SEQ_VEL_SYNTH_ACCENT 0.9f

typedef struct {
    int8_t key;     // which key played it (for visuals), -1 if none
    uint8_t count;  // 0 = rest
    uint8_t notes[MUSIC_MAX_NOTES];
} SeqNote;

typedef struct {
    uint8_t drums[SEQ_STEPS];  // bit n = drum lane n hits on this step
    SeqNote synth[SEQ_STEPS];
    uint16_t accents;          // bit s = step s is accented
} SeqPattern;

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
} SeqStatus;

// velocity 0 means note-off.
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
void seq_clear(void);                       // the current slot
void seq_reset(void);  // a new song: empty slots, starter groove in A, no swing / song
void seq_set_pattern(const SeqPattern *p);  // the current slot

// Switch pattern: immediately when stopped, at the next bar while playing.
void seq_select_slot(int slot);
// Copy the current pattern into `slot` and switch to it.
void seq_copy_to_slot(int slot);

void seq_set_drum(int step, int lane, bool on);
void seq_set_synth(int step, const SeqNote *note);  // count 0 clears
void seq_set_accent(int step, bool on);

// Live recording (only acts while playing and recording): the hit lands on
// the nearest step.
void seq_record_drum(int lane);
void seq_record_synth(const SeqNote *note);

SeqStatus seq_status(void);
void seq_snapshot(SeqPattern *out);  // the current slot

// Whole-bank access for saving and loading.
void seq_get_slot(int slot, SeqPattern *out);
void seq_put_slot(int slot, const SeqPattern *p);
