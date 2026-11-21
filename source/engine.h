// engine.h -- the sound engine: a thin, thread-safe wrapper around AMY.
//
// The main (UI) thread calls the note/patch/param functions; the audio
// thread calls engine_render_block(). A single lock serialises the two, so
// AMY never sees an event arrive half-way through rendering a block.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// AMY's synth numbers. Parts 0..PARTS-1 are synths 1..PARTS (each with its
// own sound); synth 10 is AMY's built-in General MIDI drum kit.
#define PARTS 3
#define SYNTH_LEAD 1  // part 0
#define SYNTH_DRUMS 10  // how the sequencer and keys address the drums (engine.c maps it to the lanes)
#define DRUM_SYNTH0 11  // drum lane n plays on AMY synth DRUM_SYNTH0 + n
static inline int part_synth(int part) { return 1 + part; }

#define LEAD_VOICES 6

typedef enum {
    PARAM_TONE,    // tilt EQ: dark <-> bright
    PARAM_ECHO,    // echo send level
    PARAM_REVERB,  // reverb level
    PARAM_VOLUME,  // master volume
    PARAM_COUNT
} EngineParam;

void engine_init(void);
void engine_exit(void);

// --- main thread ---------------------------------------------------------
void engine_note_on(int synth, int note, float velocity);
void engine_note_off(int synth, int note);
// Load a sound from sounds.c onto a part's synth. For a BANK sound, `patch`
// is the AMY patch number to use; CUSTOM sounds ignore it.
// `edit` (may be NULL) is the part's EDIT layer, applied on top.
#include "synthedit.h"
void engine_load_sound(int part, int sound, int patch, const SynthEdit *edit);
// Apply a changed EDIT live (reloading the patch only when its structure,
// e.g. MONO or UNISON, changed). Call after the macros have been set.
void engine_apply_edit(int part, const SynthEdit *edit);
// The DRAW sound's wave for a part: one cycle of DRAW_CYCLE samples (-1..1).
void engine_load_wave(int part, const int16_t *cycle, int n);
// The drum kit: each lane's sample, tuning and level (drums.h).
#include "drums.h"
void engine_set_kit(const DrumKit *k);
// The SAMPLE sound's sample for a part (PART_SAMPLE_PRESET + part): middle
// C plays it at its own pitch.
bool engine_load_part_sample(int part, const int16_t *data, int frames, int rate);
// A lane's own sample (for DRUM_USER_PRESET + lane), mono at `rate` Hz.
bool engine_load_drum_sample(int lane, const int16_t *data, int frames, int rate);
// Pitch bend, in semitones (+-2 at full tilt), on every part.
void engine_pitch_bend(float semitones);
// Set macro m (0..1) of the CUSTOM sound a part is playing.
void engine_set_macro(int part, int sound, int m, float value);
// Replace the MIC sound's sample (mono, `rate` Hz). Silences the lead
// synth first so no voice is playing the old buffer.
// The whole take goes in with its edit (sampler.h): the START..END region
// becomes the pitched sample, and with CHOP on each slice is loaded too,
// and parts playing MIC play one slice per key.
#include "sampler.h"
bool engine_load_sample(const int16_t *data, int frames, int rate, const SampleEdit *edit);
// value is normalised 0..1; the engine maps it to the real range.
void engine_set_param(EngineParam param, float value);  // on the master bus
// A MIX page strip (mixer.h): level, pan, DRIVE, CRUSH, TONE, and whether it
// can be heard (mute / solo).
struct MixStrip;
void engine_set_strip(int strip, const struct MixStrip *s, bool audible);
const char *engine_param_name(EngineParam param);

// --- audio thread --------------------------------------------------------
// Renders one AMY block (engine_block_frames() stereo frames, interleaved
// L/R int16) and returns a pointer to it. Valid until the next call.
const int16_t *engine_render_block(void);
bool engine_two_cores(void);  // the second core is helping render
uint32_t engine_core1_stalls(void);  // times core 1 didn't start its share in time (core 0 did it)
float engine_core1_share(void);      // the share of the oscillators core 1 is given now
int engine_block_frames(void);
int engine_sample_rate(void);
