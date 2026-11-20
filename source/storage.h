// storage.h -- save and restore the whole instrument state as one small
// binary file. Plain stdio: on the 3DS the path is on the SD card
// ("sdmc:/3ds/vibe-machine/state.vms"); tests/ round-trips it on the host.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "engine.h"
#include "fx.h"
#include "mixer.h"
#include "sampler.h"
#include "seq.h"
#include "sounds.h"
#include "tape.h"

#define STORAGE_DIR "sdmc:/3ds/vibe-machine"
#define STORAGE_PATH STORAGE_DIR "/state.vms"

typedef struct {
    char name[17];  // the song this state belongs to ("" = unnamed)
    bool unsaved;   // the song had changes not yet saved to its folder (autosave only)
    uint8_t tape_changed;  // bit t: tape track t changed since the song was loaded/saved
    bool mic_changed;      // the MIC take changed since then
    int theme;
    struct {
        int sound;   // -1 = not set (older files): use the part's default
        int patch;
        float macro[SOUND_MACROS];
    } parts[PARTS];
    int part;        // part being played / edited
    uint8_t muted;   // bit n = part n muted; bit PARTS = the drums
    MixStrip mix[MIX_STRIPS];  // the MIX page (older files: defaults)
    uint8_t solo;    // bit n = strip n soloed
    int8_t tape_source[TAPE_TRACKS];  // TAPE_SRC_ALL or a MIX strip
    SampleEdit sample_edit;  // the MIC sound's START / END / CHOP
    SeqArrangement arr;      // the SONG sheet
    uint8_t tape_muted;      // bit t = tape track t muted
    bool old_mic_macros;     // loaded from before MIC's first macro became DECAY (not saved)
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
