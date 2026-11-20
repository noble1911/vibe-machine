// mic.h -- the 3DS microphone, recording into one take buffer.
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool mic_init(void);  // false if the mic service is unavailable
void mic_exit(void);

void mic_start(void);
// Pull in whatever the mic has captured since the last call. Returns false
// once the take is full (the caller should stop).
bool mic_poll(void);
// Stops recording; returns the take and its length in frames.
int16_t *mic_stop(int *frames);

bool mic_recording(void);
float mic_level(void);    // recent peak, 0..1, for a meter
int mic_frames(void);     // frames captured so far
