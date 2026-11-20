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

// Wire command (for synth 1) that sets macro m of sound `index` to t
// (0..1). Returns false if the sound has no such macro.
bool sound_macro_wire(int index, int m, float t, char *out, size_t len);
