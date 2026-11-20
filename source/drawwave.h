// drawwave.h -- the DRAW sound's waveform: one cycle, drawn with the stylus
// as DRAW_POINTS heights, turned into a smooth looping sample to play.
// No 3DS or AMY dependencies.
#pragma once

#include <stdint.h>

#define DRAW_POINTS 64
#define DRAW_CYCLE 256  // samples in the cycle AMY loops

typedef struct {
    int8_t pt[DRAW_POINTS];  // -127..127
} DrawWave;

// A starting shape: 0 sine, 1 saw, 2 square, 3 triangle.
void draw_preset(DrawWave *w, int shape);
// Set the points between x0 and x1 (0..1 across the cycle) to a line from
// y0 to y1 (-1..1): what a stylus stroke does.
void draw_stroke(DrawWave *w, float x0, float y0, float x1, float y1);
// One cycle of DRAW_CYCLE samples: the points joined smoothly (cubic),
// with the average taken out (no DC) and the peak brought to 0.9.
void draw_render(const DrawWave *w, int16_t out[DRAW_CYCLE]);
