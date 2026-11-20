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

// --- piano roll ------------------------------------------------------------
// The roll spans MIDI notes ROLL_LOW..ROLL_HIGH. In scale mode its rows are
// only the notes of C major; in chromatic mode every semitone.
#define ROLL_LOW 36   // C2
#define ROLL_HIGH 96  // C7

bool music_in_scale(int pitch);
int music_roll_rows(bool chromatic);
int music_roll_pitch(int row, bool chromatic);  // row 0 = lowest
int music_roll_row(int pitch, bool chromatic);  // the row at or just below pitch
// Which of the 8 keys plays this pitch's scale degree (0..6), or -1.
int music_key_of_pitch(int pitch);
// "C4", "F#3"...: name of a MIDI note (valid until the next call).
const char *music_note_name(int pitch);
