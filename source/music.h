// music.h -- pure note logic: scales, chords and the drum map.
// No 3DS or AMY dependencies, so it can be unit-tested on a PC later.
#pragma once

#include <stdbool.h>

#define MUSIC_KEYS 8
#define MUSIC_MAX_NOTES 3
#define MUSIC_ROOT 60  // middle C
#define MUSIC_OCTAVE_MIN -3
#define MUSIC_OCTAVE_MAX 3

typedef enum { PLAY_NOTE, PLAY_CHORD, PLAY_DRUM } PlayMode;

// Fills notes[] for key 0..7 and returns how many there are.
//  PLAY_NOTE : one note of the major scale (key 7 is the octave)
//  PLAY_CHORD: the diatonic triad built on that scale degree
//  PLAY_DRUM : one General MIDI drum note
int music_key_notes(int key, PlayMode mode, int octave, int notes[MUSIC_MAX_NOTES]);

// Short label for a key in a mode, e.g. "C", "Dm", "KICK".
const char *music_key_label(int key, PlayMode mode);
