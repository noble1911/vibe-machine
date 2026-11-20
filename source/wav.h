// wav.h -- minimal mono 16-bit PCM WAV files (what the tape and the MIC
// take are saved as, so they open in any audio editor). Plain stdio.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool wav_write(const char *path, const int16_t *data, int frames, int rate);
// Reads up to max_frames of a mono or stereo 16-bit file (stereo is mixed
// down). Returns frames read, or -1 if the file is missing or not 16-bit
// PCM; *rate receives the file's sample rate.
int wav_read(const char *path, int16_t *data, int max_frames, int *rate);

// A WAV written a block at a time (the mixdown: stereo, as long as it
// takes). The header's sizes are filled in on close.
typedef struct {
    FILE *f;
    int rate, channels;
    uint32_t frames;
    bool ok;
} WavStream;
bool wav_stream_open(WavStream *w, const char *path, int rate, int channels);
bool wav_stream_write(WavStream *w, const int16_t *data, int frames);  // interleaved
bool wav_stream_close(WavStream *w);  // false if anything along the way failed
