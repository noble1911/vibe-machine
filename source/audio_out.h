// audio_out.h -- streams engine blocks to the 3DS DSP through ndsp.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SCOPE_POINTS 256

typedef struct {
    float load;       // smoothed time a block takes (sequencer, synth, tape, FX, capture) / real time (0..1+)
    float load_peak;  // worst block in the last second
    float level;      // smoothed output peak (0..1)
    uint32_t blocks;  // blocks rendered since start
    uint32_t late;    // times the DSP ran dry before we refilled it
} AudioStats;

// Returns false when ndsp cannot start -- usually a missing
// sdmc:/3ds/dspfirm.cdc. The app keeps running silently in that case.
bool audio_out_init(void);
void audio_out_exit(void);

AudioStats audio_out_stats(void);
// Copies the most recent output (mono, SCOPE_POINTS samples) for drawing.
void audio_out_scope(int16_t out[SCOPE_POINTS]);

// The mixdown: while capturing, every block that goes to the speakers is
// also copied into a ring (about 3 s); the main thread drains it into a WAV.
// If the ring fills (the SD card too slow), blocks are dropped and counted,
// never waited for.
void audio_out_capture(bool on);
int audio_out_capture_read(int16_t *out, int max_frames);  // interleaved stereo; returns frames
uint32_t audio_out_capture_dropped(void);  // frames dropped since capture started
