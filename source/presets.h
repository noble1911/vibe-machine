// presets.h -- saved sounds: a part's sound with its patch, macros, EDIT
// layer and (for DRAW) its waveform, as small files in
// STORAGE_DIR "/presets" (listed like the sample library). Plain stdio.
#pragma once

#include <stdbool.h>

#include "drawwave.h"
#include "synthedit.h"

typedef struct {
    int sound, patch;
    float macro[2];
    SynthEdit edit;
    DrawWave wave;
} Preset;

bool preset_save(const char *path, const Preset *p);
bool preset_load(const char *path, Preset *p);  // false if missing / damaged
