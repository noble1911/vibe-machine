// music.c -- see music.h.

#include "music.h"

#include <stdio.h>

static const int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};

// GM drum notes, all present in AMY's built-in kit (patch 258).
static const int kDrums[MUSIC_KEYS] = {36, 38, 42, 46, 39, 37, 45, 49};

static const char *const kNoteLabels[MUSIC_KEYS] = {"C", "D", "E", "F", "G", "A", "B", "C+"};
static const char *const kChordLabels[MUSIC_KEYS] = {"C", "Dm", "Em", "F", "G", "Am", "Bdim", "C+"};
static const char *const kDrumLabels[MUSIC_KEYS] = {"KICK", "SNARE", "HAT", "OPEN", "CLAP", "RIM", "TOM", "CRASH"};

static int degree_note(int degree, int octave) {
    return MUSIC_ROOT + 12 * (octave + degree / 7) + kMajor[degree % 7];
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
    static const bool in[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    return in[((pitch % 12) + 12) % 12];
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
    static const int key[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    return key[((pitch % 12) + 12) % 12];
}

const char *music_note_name(int pitch) {
    static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s%d", names[((pitch % 12) + 12) % 12], pitch / 12 - 1);
    return buf;
}

const char *music_key_label(int key, PlayMode mode) {
    if (key < 0 || key >= MUSIC_KEYS) return "";
    switch (mode) {
        case PLAY_DRUM: return kDrumLabels[key];
        case PLAY_CHORD: return kChordLabels[key];
        case PLAY_NOTE:
        default: return kNoteLabels[key];
    }
}
