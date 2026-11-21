// sounds.h -- the instruments you can pick on the SYNTH page.
//
// Two kinds:
//  * BANK   -- a range of AMY's built-in patches (Juno-6, DX7). The SYNTH
//              page shows them as a grid of patch dots.
//  * CUSTOM -- Vibe Machine's own voices, each an AMY patch string with two
//              "macro" controls driven by the XY creature pad.
// Pure data + string formatting: no 3DS dependencies, so the host probe
// (tests/) can load and check every sound.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOUND_MACROS 2

typedef enum { SOUND_BANK, SOUND_CUSTOM } SoundKind;

typedef struct {
    const char *name;   // short, shouty: "PLUCK"
    const char *tag;    // four letters max, for the tiny SYNTH tiles
    const char *blurb;  // one line for the top screen
    uint8_t motif;      // MotifKind used as the sound's mascot
    SoundKind kind;
    int bank_first;     // BANK: first AMY patch number
    int bank_count;     // BANK: how many patches
    int voices;         // polyphony
    float level;        // AMY synth_level trim so every sound sits at a similar loudness
    const char *patch;  // CUSTOM: AMY patch string (wire commands, Z-separated)
    const char *macro_name[SOUND_MACROS];
    float macro_default[SOUND_MACROS];  // 0..1
} Sound;

int sound_count(void);
const Sound *sound_get(int index);

// Wire command that sets macro m of sound `index`, playing on AMY synth
// `synth`, to t (0..1). Returns false if the sound has no such macro.
bool sound_macro_wire(int index, int m, float t, int synth, char *out, size_t len);

// Index of the MIC sound (the one that plays the microphone take).
int sound_mic_index(void);
// Index of the SAMPLE sound: each part plays its own sample, memory preset
// PART_SAMPLE_PRESET + part.
#define PART_SAMPLE_PRESET 250
int sound_sample_index(void);
// The SYNTH page shows the sounds in pages of SOUNDS_PER_PAGE tiles.
#define SOUNDS_PER_PAGE 11

// --- DRAW and EDIT --------------------------------------------------------
#include "synthedit.h"

// The DRAW sound plays one drawn cycle, looped: memory PCM preset
// DRAW_PRESET + part (each part draws its own).
#define DRAW_PRESET 220
int sound_draw_index(void);

// Where the EDIT page's controls start for a sound: its own envelope,
// filter and so on, as designed.
void sound_edit_defaults(int index, float out[ED_PARAMS]);

// The edit's structure: what needs the patch reloaded to change (voice
// count, the extra LFO oscillator, unison). Two edits with the same key can
// be swapped live.
uint32_t sound_edit_key(int index, const SynthEdit *e);
// Voices a part gets: MONO is one; UNISON divides the sound's voices so the
// oscillator count (the CPU cost) never goes up.
int sound_edit_voices(int index, const SynthEdit *e);
// The patch string with the edit's structure folded in (voice-relative).
void sound_edit_patch(int index, const SynthEdit *e, char *out, size_t len);
// The edit's live commands for AMY synth `synth`: up to `max` wire messages
// of `width` chars each, written row by row into `out`. Returns the count.
int sound_edit_wire(int index, const SynthEdit *e, int synth, char *out, int width, int max);
// DX7's BRIGHT: the factor its modulators' levels are scaled by (1 = as
// designed). engine.c applies it: that needs the patch's own levels.
float sound_edit_bright(int index, const SynthEdit *e);
int sound_dx7_index(void);
// UNISON on a sound: 0 it can't (FM, KS, samples), 1 it can, 2 it's built in
// (SUPERSAW: DETUNE spreads its own five saws).
int sound_unison_kind(int index);
