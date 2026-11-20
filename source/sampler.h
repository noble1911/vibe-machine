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
// the keys; otherwise it's cut into `slices` equal slices, one per key.
typedef struct {
    float start, end;  // 0..1, start < end
    uint8_t slices;    // 0, or 2..SAMPLER_MAX_SLICES
} SampleEdit;

static inline void sampler_edit_default(SampleEdit *e) {
    e->start = 0.0f;
    e->end = 1.0f;
    e->slices = 0;
}
// Keep an edit sane (order, minimum length, slice count).
void sampler_edit_clamp(SampleEdit *e);

// Frames of a `frames`-long take that the region (slice < 0) or slice
// `slice` covers. Returns the length and sets *first.
int sampler_region(const SampleEdit *e, int frames, int slice, int *first);

// CHOP: the slice a MIDI note plays. C4 is slice 0, then up the C major
// scale (the keys), wrapping round; sharps play the slice below.
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
