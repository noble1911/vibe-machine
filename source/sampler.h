// sampler.h -- turning a raw microphone take into a playable sample.
// Pure logic (no 3DS headers), so tests/ can check it on the build machine.
#pragma once

#include <stdint.h>

#define SAMPLER_RATE 16364           // the 3DS mic's 16.36 kHz mode
#define SAMPLER_MAX_FRAMES (SAMPLER_RATE * 3)
#define SAMPLER_PRESET 200           // AMY memory PCM preset the MIC sound plays
#define SAMPLER_OVERVIEW 96          // points in the waveform picture

// Trim leading silence, normalise the peak to 0.9 and fade the edges, in
// place. Returns the new length (data starts at buf[0]); 0 if it was silent.
int sampler_tidy(int16_t *buf, int frames);

// Peak level (0..1) of each of `points` equal slices, for drawing.
void sampler_overview(const int16_t *buf, int frames, float *out, int points);

// A cartoon "boing" so the MIC sound has something to play before you've
// recorded anything. Returns the number of frames written.
int sampler_boing(int16_t *buf, int max_frames);
