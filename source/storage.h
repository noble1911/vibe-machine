// storage.h -- save and restore the whole instrument state as one small
// binary file. Plain stdio: on the 3DS the path is on the SD card
// ("sdmc:/3ds/vibe-machine/state.vms"); tests/ round-trips it on the host.
#pragma once

#include "drawwave.h"
#include "drums.h"
#include "synthedit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "engine.h"
#include "fx.h"
#include "mixer.h"
#include "sampler.h"
#include "seq.h"
#include "sounds.h"
#include "tape.h"

#ifdef VM_STORY
#define STORAGE_DIR "sdmc:/3ds/vibe-machine-story"  // the scripted demo never touches your files
#else
#define STORAGE_DIR "sdmc:/3ds/vibe-machine"
#endif
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
        PartEdit pe;    // the SYNTH page's EDIT layer, A/B snapshots, MORPH
        DrawWave wave;  // the DRAW sound's wave
    } parts[PARTS];
    bool has_edit;   // pe / wave were in the file (version 16); else use defaults (not saved)
    DrumKit kit;     // the drum kit (version 17; older songs: the TR-808)
    int part;        // part being played / edited
    uint8_t muted;   // bit n = part n muted; bit PARTS = the drums
    MixStrip mix[MIX_STRIPS];  // the MIX page (older files: defaults)
    uint8_t solo;    // bit n = strip n soloed
    int8_t tape_source[TAPE_TRACKS];  // TAPE_SRC_ALL or a MIX strip
    SampleEdit sample_edit;  // the MIC sound's START / END / CHOP
    SeqArrangement arr;      // the SONG sheet
    uint8_t tape_muted;      // bit t = tape track t muted
    uint8_t key_root, key_scale;  // the song's key
    SeqArp arp;
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

// Small files written so that losing power never loses them: the new
// contents go to <path>.new, the old file becomes <path>.bak, and the new
// one takes its place (the SD card's rename won't replace a file). Some
// complete copy is always on the card; storage_read_newest() loads the
// first of path, .new, .bak that `load` accepts (checksummed formats only).
#define STORAGE_PATH_MAX 192
bool storage_write_file(const char *path, const void *data, size_t n);
bool storage_read_newest(const char *path, bool (*load)(const char *path, void *out), void *out);
bool storage_exists(const char *path);  // any of the three
void storage_remove(const char *path);  // all three
