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
#define SYNTH_DRUMS 10
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
void engine_load_sound(int part, int sound, int patch);
// Set macro m (0..1) of the CUSTOM sound a part is playing.
void engine_set_macro(int part, int sound, int m, float value);
// Replace the MIC sound's sample (mono, `rate` Hz). Silences the lead
// synth first so no voice is playing the old buffer.
bool engine_load_sample(const int16_t *data, int frames, int rate);
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
int engine_block_frames(void);
int engine_sample_rate(void);
