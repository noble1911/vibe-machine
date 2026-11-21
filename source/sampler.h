// sampler.h -- turning a raw microphone take into a playable sample.
// Pure logic (no 3DS headers), so tests/ can check it on the build machine.
#pragma once

#include <stdint.h>

#define SAMPLER_RATE 16364           // the 3DS mic's 16.36 kHz mode
#define SAMPLER_SECONDS 4            // a take, or a bar grabbed from tape (down to 60 BPM)
#define SAMPLER_MAX_FRAMES (SAMPLER_RATE * SAMPLER_SECONDS)
#define SAMPLER_PRESET 200           // AMY memory PCM preset the MIC sound plays (START..END)
#define SAMPLER_MAX_SLICES 8         // CHOP: slice k is preset SAMPLER_PRESET + 1 + k
#define SAMPLER_SLICE_NOTE 60        // slices play at their own pitch (the preset's root)
#define SAMPLER_OVERVIEW 96          // points in the waveform picture

// Trim leading silence, normalise the peak to 0.9 and fade the edges, in
// place. Returns the new length (data starts at buf[0]); 0 if it was silent.
int sampler_tidy(int16_t *buf, int frames);

// Peak level (0..1) of each of `points` equal slices, for drawing.
void sampler_overview(const int16_t *buf, int frames, float *out, int points);

// The MIC sound's edit of the take: which part plays (START..END, as
// fractions of the take), and CHOP: 0 = the region plays pitched across
// the keys; otherwise it's cut into `slices` slices, one per key, at the
// cut points (fractions of the take, in order between START and END:
// slice k runs from the cut before it to the cut after it).
typedef struct {
    float start, end;  // 0..1, start < end
    uint8_t slices;    // 0, or 2..SAMPLER_MAX_SLICES
    float cut[SAMPLER_MAX_SLICES - 1];  // the first slices - 1 are used; the rest are 0
} SampleEdit;

static inline void sampler_edit_default(SampleEdit *e) {
    e->start = 0.0f;
    e->end = 1.0f;
    e->slices = 0;
    for (int k = 0; k < SAMPLER_MAX_SLICES - 1; k++) e->cut[k] = 0.0f;
}
// Keep an edit sane: order, minimum length, slice count, and the cuts in
// order inside START..END with a little room between them (pushed along,
// not reordered, when START or END are dragged over them).
void sampler_edit_clamp(SampleEdit *e);
// Equal slices: the cuts spread evenly between START and END.
void sampler_edit_even(SampleEdit *e);
// Where slice boundary b is (0 = START, slices = END), as a fraction.
float sampler_bound(const SampleEdit *e, int b);

// CHOP to the hits: put the cuts on the strongest attacks (drum hits,
// syllables) between START and END, just before each one and on a zero
// crossing. Where there are fewer hits than cuts, the longest slices are
// split evenly. Returns how many cuts landed on a hit.
int sampler_auto_chop(const int16_t *buf, int frames, SampleEdit *e);
// The nearest zero crossing to `pos` (a fraction of the take), within a
// couple of milliseconds; `pos` itself if there isn't one. A slice that
// starts on one doesn't click.
float sampler_snap(const int16_t *buf, int frames, float pos);

// Frames of a `frames`-long take that the region (slice < 0) or slice
// `slice` covers. Returns the length and sets *first.
int sampler_region(const SampleEdit *e, int frames, int slice, int *first);

// CHOP: the slice a MIDI note plays. The root above middle C is slice 0,
// then up the song's scale (the keys), wrapping round; notes outside the
// scale play the slice below.
int sampler_slice_of_pitch(int pitch, int slices);

// Linear-interpolating resample of `n` frames at in_rate to out_rate.
// Returns frames written (at most max_out).
int sampler_resample(const int16_t *in, int n, int in_rate, int16_t *out, int max_out, int out_rate);

// Normalise the peak to 0.9 and fade the very edges, without trimming (for
// loops, where timing matters). Silence is left alone.
void sampler_normalise(int16_t *buf, int frames);

// A cartoon "boing" so the MIC sound has something to play before you've
// recorded anything. Returns the number of frames written.
int sampler_boing(int16_t *buf, int max_frames);
