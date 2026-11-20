// fx.h -- performance effects on the master output, played from the FX pad.
//
//   CRUSH   X: sample-rate reduction   Y: bit depth
//   SWEEP   X: low-pass <- off -> high-pass   Y: resonance
//   STUTTER X: repeat length (1/2 .. 1/32 note, tempo-synced)  Y: gate
//   TAPE    X: how fast the tape stops   Y: wow & flutter
//
// fx_process() runs on the audio thread after AMY renders each block; the
// fx_set*() calls come from the UI thread. Parameters are single floats, so
// a torn read can at worst use one stale value for one block.
// No 3DS dependencies: tests/ exercises it on the build machine.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { FX_CRUSH, FX_SWEEP, FX_STUTTER, FX_TAPE, FX_COUNT } FxKind;

typedef struct {
    bool on[FX_COUNT];
    float x[FX_COUNT], y[FX_COUNT];
    float tape_speed;  // 1 = normal, 0 = stopped (for visuals)
} FxState;

void fx_init(int sample_rate);
void fx_set(FxKind k, bool on, float x, float y);
void fx_set_tempo(float bpm);
FxState fx_state(void);
const char *fx_name(FxKind k);
const char *fx_axis_name(FxKind k, int axis);  // axis 0 = X, 1 = Y

// In place, interleaved stereo int16.
void fx_process(int16_t *stereo, int frames);
