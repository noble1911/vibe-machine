// music.c -- see music.h.

#include "music.h"

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

const char *music_key_label(int key, PlayMode mode) {
    if (key < 0 || key >= MUSIC_KEYS) return "";
    switch (mode) {
        case PLAY_DRUM: return kDrumLabels[key];
        case PLAY_CHORD: return kChordLabels[key];
        case PLAY_NOTE:
        default: return kNoteLabels[key];
    }
}
