// music.h -- pure note logic: scales, chords and the drum map.
// No 3DS or AMY dependencies, so it can be unit-tested on a PC later.
#pragma once

#include <stdbool.h>

#define MUSIC_KEYS 8
#define MUSIC_MAX_NOTES 3
#define MUSIC_ROOT 60  // middle C
#define MUSIC_OCTAVE_MIN -3
#define MUSIC_OCTAVE_MAX 3
#define MUSIC_SCALES 8

// The song's key: a root (0..11, C..B) and one of MUSIC_SCALES scales. The
// keys, chords, the roll's scale rows and the dice all follow it. Notes
// already written stay where they are.
void music_set_key(int root, int scale);
int music_root(void);
int music_scale(void);
const char *music_scale_name(int scale);  // "MINOR PENTA"...
const char *music_root_name(int root);    // "C#"...
// Scale degree of a pitch, counting from the root above middle C (0) up and
// down through the octaves; notes outside the scale take the degree below.
int music_degree(int pitch);

typedef enum { PLAY_NOTE, PLAY_CHORD, PLAY_DRUM } PlayMode;

// Fills notes[] for key 0..7 and returns how many there are.
//  PLAY_NOTE : one note of the scale, walking up from the root (a 7-note
//              scale's key 7 is the octave; shorter scales carry on up)
//  PLAY_CHORD: the triad stacked in thirds within the scale on that degree
//  PLAY_DRUM : one General MIDI drum note
int music_key_notes(int key, PlayMode mode, int octave, int notes[MUSIC_MAX_NOTES]);

// Short label for a key in a mode, e.g. "C", "Dm", "KICK" (valid until the
// key's label is asked for again).
const char *music_key_label(int key, PlayMode mode);

// --- piano roll ------------------------------------------------------------
// The roll spans MIDI notes ROLL_LOW..ROLL_HIGH. In scale mode its rows are
// only the notes of the key; in chromatic mode every semitone.
#define ROLL_LOW 36   // C2
#define ROLL_HIGH 96  // C7

bool music_in_scale(int pitch);
int music_roll_rows(bool chromatic);
int music_roll_pitch(int row, bool chromatic);  // row 0 = lowest
int music_roll_row(int pitch, bool chromatic);  // the row at or just below pitch
// Which key plays this pitch's scale degree (in the first octave), or -1
// if it's outside the scale.
int music_key_of_pitch(int pitch);
// "C4", "F#3"...: name of a MIDI note (valid until the next call).
const char *music_note_name(int pitch);
