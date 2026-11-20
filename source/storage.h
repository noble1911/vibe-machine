// storage.h -- save and restore the whole instrument state as one small
// binary file. Plain stdio: on the 3DS the path is on the SD card
// ("sdmc:/3ds/vibe-machine/state.vms"); tests/ round-trips it on the host.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "engine.h"
#include "fx.h"
#include "seq.h"
#include "sounds.h"

#define STORAGE_DIR "sdmc:/3ds/vibe-machine"
#define STORAGE_PATH STORAGE_DIR "/state.vms"

typedef struct {
    char name[17];  // the song this state belongs to ("" = unnamed)
    bool unsaved;   // the song had changes not yet saved to its folder (autosave only)
    int theme;
    int sound;
    int patch;
    float macro[SOUND_MACROS];
    float params[PARAM_COUNT];
    int octave;
    int bpm;
    SeqPattern slots[SEQ_SLOTS];
    int slot;     // pattern being played / edited
    bool song;
    float swing;
    float fx_x[FX_COUNT], fx_y[FX_COUNT];
} SaveState;

bool storage_save(const char *path, const SaveState *st);
// Returns false (and leaves *st untouched) if the file is missing, from a
// different version, or damaged.
bool storage_load(const char *path, SaveState *st);
