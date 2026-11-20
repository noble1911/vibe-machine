// songs.h -- named songs on the SD card.
//
// A song is a folder under STORAGE_DIR "/songs": the instrument state
// (song.vms, the same format as the autosave) plus its tape tracks
// (tape1..4.wav) and MIC take (mic.wav), which main.c writes with wav.c.
// Plain stdio / dirent, so tests/ can run it on the build machine.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "storage.h"

#define SONG_NAME_MAX 16  // characters, not counting the terminator
#define SONGS_MAX 64

typedef struct {
    char name[SONG_NAME_MAX + 1];
    int bpm;
    int patterns;  // filled pattern slots
} SongInfo;

// Upper-case letters, digits, spaces and dashes; trimmed; never empty.
void song_clean_name(const char *in, char *out);
// A cheerful random name like "NEON GECKO 7".
void song_random_name(uint32_t seed, char *out);

// Path of `file` inside song `name` under `root` (e.g. ".../songs").
void song_path(const char *root, const char *name, const char *file, char *out, size_t len);

bool song_save_state(const char *root, const char *name, const SaveState *st);
bool song_load_state(const char *root, const char *name, SaveState *st);
bool song_exists(const char *root, const char *name);
// Removes the song's files and folder.
bool song_delete(const char *root, const char *name);
// Fills `out` with up to `max` songs, sorted by name. Returns the count.
int song_list(const char *root, SongInfo *out, int max);
