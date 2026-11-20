// engine.h -- the sound engine: a thin, thread-safe wrapper around AMY.
//
// The main (UI) thread calls the note/patch/param functions; the audio
// thread calls engine_render_block(). A single lock serialises the two, so
// AMY never sees an event arrive half-way through rendering a block.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// AMY's synth numbers. Synth 1 is the playable instrument; synth 10 is
// AMY's built-in General MIDI drum kit.
#define SYNTH_LEAD 1
#define SYNTH_DRUMS 10

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
void engine_set_patch(int patch);
// Load a sound from sounds.c onto the lead synth. For a BANK sound, `patch`
// is the AMY patch number to use; CUSTOM sounds ignore it.
void engine_load_sound(int sound, int patch);
// Set macro m (0..1) of the CUSTOM sound currently loaded.
void engine_set_macro(int sound, int m, float value);
// value is normalised 0..1; the engine maps it to the real range.
void engine_set_param(EngineParam param, float value);
const char *engine_param_name(EngineParam param);

// --- audio thread --------------------------------------------------------
// Renders one AMY block (engine_block_frames() stereo frames, interleaved
// L/R int16) and returns a pointer to it. Valid until the next call.
const int16_t *engine_render_block(void);
int engine_block_frames(void);
int engine_sample_rate(void);
