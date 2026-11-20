// wav.h -- minimal mono 16-bit PCM WAV files (what the tape and the MIC
// take are saved as, so they open in any audio editor). Plain stdio.
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool wav_write(const char *path, const int16_t *data, int frames, int rate);
// Reads up to max_frames of a mono or stereo 16-bit file (stereo is mixed
// down). Returns frames read, or -1 if the file is missing or not 16-bit
// PCM; *rate receives the file's sample rate.
int wav_read(const char *path, int16_t *data, int max_frames, int *rate);
