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
// `safe_point` (may be NULL) is called once per block at the moment nothing
// is rendering: the place for the sequencer to play its notes (they sound
// in the NEXT block handed back: the engine runs one block ahead).
const int16_t *engine_render_block(void (*safe_point)(void));
const char *engine_core_report(void);  // what the system said about a thread on core 1 (for the perf log)
// How much of core 1 the app asks for (percent; 0 = render on core 0 only).
// A tuning control: L + R + SELECT steps through a few values.
void engine_set_core1_limit(int percent);
int engine_core1_limit(void);
bool engine_two_cores(void);  // the second core is helping render
uint32_t engine_core1_stalls(void);  // times core 1 didn't start its share in time (core 0 did it)
float engine_core1_share(void);      // the share of the oscillators core 1 is given now
// UI thread: several engine calls as one (a chord's notes, a sound and its
// macros). Each call otherwise waits for the engine's next safe point (up
// to a block, 4 ms); a batch waits once. Keep them short: the audio thread
// waits at its safe point until the batch ends.
void engine_batch_begin(void);
void engine_batch_end(void);
// Audio thread, once a block's work is all done (the engine's and
// everything after it): how long the whole block took. The core split
// balances on it.
void engine_block_done(uint32_t total_ticks);
// Where a block's time goes, as fractions of the block's own length (1.0 =
// all of real time), smoothed over ~0.25 s. total_max: the worst block
// since the last call.
typedef struct {
    float render0;  // core 0 rendering its oscillators
    float core1;    // core 1 rendering its (in parallel)
    float fill;     // AMY's mixdown: buses, echo, reverb, EQ, the mixer
    float post;     // PUNCH, FX, tape, capture
    float total, total_max;
    float oscs;     // oscillators sounding
    float share;    // core 1's share of them
    float share_used;  // (internal: the smoothed share)
    float wait_max;    // the longest wait for core 1 to finish its share (blocks)
    float park_max;    // the longest the audio thread stood aside for UI calls (blocks)
    uint32_t stalls;
} EnginePerf;
EnginePerf engine_perf(void);
int engine_block_frames(void);
int engine_sample_rate(void);
