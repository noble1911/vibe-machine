// seq.h -- the 16-step pattern sequencer.
//
// The clock runs on the audio thread: audio_out.c calls seq_advance() before
// rendering every block, so steps follow the sample clock rather than the
// 60 Hz screen. Everything else is called from the UI thread; a lock keeps
// the two sides consistent.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "music.h"

#define SEQ_STEPS 16
#define SEQ_LANES MUSIC_KEYS  // one drum lane per key (kick, snare, hat...)
#define SEQ_BPM_MIN 40
#define SEQ_BPM_MAX 240

typedef struct {
    int8_t key;     // which key played it (for visuals), -1 if none
    uint8_t count;  // 0 = rest
    uint8_t notes[MUSIC_MAX_NOTES];
} SeqNote;

typedef struct {
    uint8_t drums[SEQ_STEPS];  // bit n = drum lane n hits on this step
    SeqNote synth[SEQ_STEPS];
} SeqPattern;

typedef struct {
    bool playing;
    bool recording;
    int bpm;
    int step;        // step most recently fired (valid while playing)
    uint32_t fired;  // increments every time a step fires
} SeqStatus;

void seq_init(void);

// --- audio thread --------------------------------------------------------
// Advance the clock by one block of `frames` at `rate` Hz and send any
// notes that fall due to the engine.
void seq_advance(int frames, int rate);

// --- UI thread -----------------------------------------------------------
void seq_toggle_play(void);
void seq_set_recording(bool on);
void seq_set_bpm(int bpm);
void seq_clear(void);

void seq_set_drum(int step, int lane, bool on);
void seq_set_synth(int step, const SeqNote *note);  // count 0 clears

// Live recording (only acts while playing and recording): the hit lands on
// the nearest step.
void seq_record_drum(int lane);
void seq_record_synth(const SeqNote *note);

SeqStatus seq_status(void);
void seq_snapshot(SeqPattern *out);
