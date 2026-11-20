// samples.h -- the sample library: WAV files in one folder on the SD card
// (STORAGE_DIR "/samples"). The MIC sound's take is saved there by name,
// and any 16-bit WAV copied in from a computer shows up too (any rate,
// mono or stereo: main.c resamples it on load).
// Plain stdio / dirent, so tests/ can run it on the build machine.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "songs.h"

typedef struct {
    char file[64];                  // the file's real name, "KICK LOOP.wav"
    char name[SONG_NAME_MAX + 1];   // shown in the list: cleaned, no extension
} SampleFile;

// Up to `max` .wav files in `root` (any case), sorted by shown name.
int sample_list(const char *root, SampleFile *out, int max);
void sample_path(const char *root, const char *file, char *out, size_t len);
// The file a sample saved under `name` (already cleaned) goes in.
void sample_file_for(const char *name, char *file, size_t len);
