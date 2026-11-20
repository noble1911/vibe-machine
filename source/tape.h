// tape.h -- a 4-track tape recorder.
//
// The tape records onto the armed track from that track's source: ALL is
// whatever you hear (after the performance FX, so a stutter or tape-stop
// performance gets captured too); a source of 0..TAPE_SOURCES-1 is one MIX
// strip on its own (parts 1-3, drums), before the master's echo, reverb and
// FX. It plays all unmuted tracks back on top of the live sound. It runs on the
// audio thread's sample clock, like the sequencer, so the two stay locked.
//
// Tracks are mono, TAPE_RATE Hz, TAPE_SECONDS long; the tape loops at the
// end. Speed can be anything from -2 (reverse, double speed) to +2, and the
// UI can scrub (scratch) it. Recording always runs at speed 1.
//
// No 3DS dependencies: tests/ exercises it on the build machine.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TAPE_TRACKS 4
#define TAPE_SOURCES 4    // MIX strips (mixer.h's MIX_STRIPS)
#define TAPE_SRC_ALL (-1)
#define TAPE_RATE 22050
#define TAPE_SECONDS 60
#define TAPE_FRAMES (TAPE_RATE * TAPE_SECONDS)
#define TAPE_PEAK_BLOCK 256  // frames per entry in the drawing overview
#define TAPE_PEAKS (TAPE_FRAMES / TAPE_PEAK_BLOCK)

typedef struct {
    bool ok;          // tape memory allocated
    bool playing;
    bool recording;   // armed track is being written
    bool scrubbing;
    int track;        // armed track
    float seconds;    // head position
    float speed;      // current speed (scrub included)
    bool muted[TAPE_TRACKS];
    float level[TAPE_TRACKS];  // playback meters, 0..1
    bool used[TAPE_TRACKS];    // has anything been recorded
    int source[TAPE_TRACKS];   // TAPE_SRC_ALL or a MIX strip
} TapeStatus;

bool tape_init(void);
void tape_exit(void);

// --- UI thread -----------------------------------------------------------
void tape_play(bool on);
void tape_record(bool on);  // arms recording; it writes while playing
void tape_select(int track);
void tape_rewind(void);
void tape_set_speed(float speed);  // -2..2, used while playing (not recording)
// While active, `speed` replaces the transport (the tape moves even when
// stopped): the scratch gesture.
void tape_scrub(bool active, float speed);
void tape_mute(int track, bool muted);
void tape_set_source(int track, int source);  // TAPE_SRC_ALL or 0..TAPE_SOURCES-1
void tape_clear(int track);
TapeStatus tape_status(void);
// Peak level (0..1) of track `track` around the overview block `block`.
float tape_peak(int track, int block);

// Saving and loading (UI thread, with the tape stopped). A track is dirty
// once it has been recorded over or wiped since it was last saved/loaded.
bool tape_dirty(int track);
const int16_t *tape_track_data(int track, int *frames);  // frames = recorded length
void tape_mark_saved(int track);
void tape_set_dirty(int track);  // force a re-save (after loading a song's tapes)
void tape_load_track(int track, const int16_t *data, int frames);
// Bumps whenever any track is recorded on, wiped or loaded (for "unsaved").
uint32_t tape_generation(void);
// Per-track version of the same counter.
uint32_t tape_track_generation(int track);

// --- audio thread --------------------------------------------------------
// io: interleaved stereo int16 at `rate` Hz (the live, post-FX mix).
// strips: TAPE_SOURCES mono blocks of the same `frames` (full scale +-1.0),
// one per MIX strip, or NULL. Records the armed track's source if armed,
// then adds the tape playback to io in place.
void tape_process(int16_t *io, const float *const *strips, int frames, int rate);
