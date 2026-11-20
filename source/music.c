// music.c -- see music.h.

#include "music.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    int n;           // notes per octave
    int steps[7];    // semitones above the root
} Scale;

static const Scale kScales[MUSIC_SCALES] = {
    {"MAJOR", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"MINOR", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"DORIAN", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"MIXOLYDIAN", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"HARMONIC MIN", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"MAJOR PENTA", 5, {0, 2, 4, 7, 9}},
    {"MINOR PENTA", 5, {0, 3, 5, 7, 10}},
    {"BLUES", 6, {0, 3, 5, 6, 7, 10}},
};

static const char *const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// GM drum notes, all present in AMY's built-in kit (patch 258).
static const int kDrums[MUSIC_KEYS] = {36, 38, 42, 46, 39, 37, 45, 49};
static const char *const kDrumLabels[MUSIC_KEYS] = {"KICK", "SNARE", "HAT", "OPEN", "CLAP", "RIM", "TOM", "CRASH"};

static int s_root;   // 0..11 (C..B)
static int s_scale;  // index into kScales

void music_set_key(int root, int scale) {
    s_root = ((root % 12) + 12) % 12;
    s_scale = scale >= 0 && scale < MUSIC_SCALES ? scale : 0;
}
int music_root(void) { return s_root; }
int music_scale(void) { return s_scale; }
const char *music_scale_name(int scale) { return kScales[scale >= 0 && scale < MUSIC_SCALES ? scale : 0].name; }
const char *music_root_name(int root) { return kNames[((root % 12) + 12) % 12]; }

// The pitch of scale degree `degree` (0 = the root at MUSIC_ROOT + root;
// may be negative or run into higher octaves).
static int degree_note(int degree, int octave) {
    const Scale *sc = &kScales[s_scale];
    int oct = degree >= 0 ? degree / sc->n : -((sc->n - 1 - degree) / sc->n);
    int d = degree - oct * sc->n;
    return MUSIC_ROOT + s_root + 12 * (octave + oct) + sc->steps[d];
}

int music_degree(int pitch) {
    const Scale *sc = &kScales[s_scale];
    int rel = pitch - (MUSIC_ROOT + s_root);
    int oct = rel >= 0 ? rel / 12 : -((11 - rel) / 12);
    int semi = rel - oct * 12, d = 0;
    for (int i = 0; i < sc->n; i++)
        if (sc->steps[i] <= semi) d = i;  // out-of-scale notes take the degree below
    return oct * sc->n + d;
}

int music_key_notes(int key, PlayMode mode, int octave, int notes[MUSIC_MAX_NOTES]) {
    if (key < 0 || key >= MUSIC_KEYS) return 0;
    switch (mode) {
        case PLAY_DRUM:
            notes[0] = kDrums[key];
            return 1;
        case PLAY_CHORD:
            // Stack thirds within the scale: degree, degree+2, degree+4.
            for (int i = 0; i < 3; i++) notes[i] = degree_note(key + 2 * i, octave);
            return 3;
        case PLAY_NOTE:
        default:
            notes[0] = degree_note(key, octave);
            return 1;
    }
}

bool music_in_scale(int pitch) {
    const Scale *sc = &kScales[s_scale];
    const int semi = (((pitch - s_root) % 12) + 12) % 12;
    for (int i = 0; i < sc->n; i++)
        if (sc->steps[i] == semi) return true;
    return false;
}

int music_roll_rows(bool chromatic) {
    int n = 0;
    for (int p = ROLL_LOW; p <= ROLL_HIGH; p++) n += chromatic || music_in_scale(p);
    return n;
}

int music_roll_pitch(int row, bool chromatic) {
    for (int p = ROLL_LOW; p <= ROLL_HIGH; p++)
        if (chromatic || music_in_scale(p)) {
            if (row-- == 0) return p;
        }
    return ROLL_HIGH;
}

int music_roll_row(int pitch, bool chromatic) {
    int row = -1;
    for (int p = ROLL_LOW; p <= ROLL_HIGH && p <= pitch; p++) row += chromatic || music_in_scale(p);
    return row < 0 ? 0 : row;
}

int music_key_of_pitch(int pitch) {
    if (!music_in_scale(pitch)) return -1;
    const int n = kScales[s_scale].n, d = ((music_degree(pitch) % n) + n) % n;
    return d < MUSIC_KEYS ? d : -1;
}

const char *music_note_name(int pitch) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s%d", kNames[((pitch % 12) + 12) % 12], pitch / 12 - 1);
    return buf;
}

// A chord's name from its notes: the root's name and the quality.
static const char *chord_name(const int *n, char *buf, size_t len) {
    const int third = n[1] - n[0], fifth = n[2] - n[0];
    const char *q = third == 4 && fifth == 7   ? ""
                    : third == 3 && fifth == 7 ? "m"
                    : third == 3 && fifth == 6 ? "dim"
                    : third == 4 && fifth == 8 ? "aug"
                    : third == 5 && fifth == 7 ? "sus4"
                    : third == 2 && fifth == 7 ? "sus2"
                                               : "*";
    snprintf(buf, len, "%s%s", kNames[((n[0] % 12) + 12) % 12], q);
    return buf;
}

const char *music_key_label(int key, PlayMode mode) {
    static char buf[MUSIC_KEYS][12];
    if (key < 0 || key >= MUSIC_KEYS) return "";
    int notes[MUSIC_MAX_NOTES];
    switch (mode) {
        case PLAY_DRUM: return kDrumLabels[key];
        case PLAY_CHORD:
            music_key_notes(key, PLAY_CHORD, 0, notes);
            return chord_name(notes, buf[key], sizeof(buf[key]));
        case PLAY_NOTE:
        default:
            music_key_notes(key, PLAY_NOTE, 0, notes);
            // The top key is an octave up from the first when the scale has 7 notes.
            snprintf(buf[key], sizeof(buf[key]), "%s%s", kNames[((notes[0] % 12) + 12) % 12],
                     key > 0 && notes[0] - MUSIC_ROOT - s_root >= 12 ? "+" : "");
            return buf[key];
    }
}
