// tables.c -- see tables.h.

#include "tables.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// One cycle from harmonic amplitudes amp[1..n] (amp[0] unused), with fixed
// spread-out phases so the wave isn't one tall spike, scaled to 0.9 peak.
static void additive(const float *amp, int n, int16_t *out) {
    static float buf[TABLE_CYCLE];
    float peak = 0.0f;
    for (int i = 0; i < TABLE_CYCLE; i++) {
        float v = 0.0f;
        for (int h = 1; h <= n; h++) {
            if (amp[h] == 0.0f) continue;
            const float phase = (float)(h * h) * 0.618f;  // a fixed, non-aligned phase per harmonic
            v += amp[h] * sinf(2.0f * (float)M_PI * h * i / TABLE_CYCLE + phase);
        }
        buf[i] = v;
        peak = fabsf(v) > peak ? fabsf(v) : peak;
    }
    const float g = peak > 0.0f ? 0.9f * 32767.0f / peak : 0.0f;
    for (int i = 0; i < TABLE_CYCLE; i++) out[i] = (int16_t)lrintf(buf[i] * g);
}

void tables_organ(int16_t *out) {
    // Drawbars as harmonics of 8': 8' = 1, 4' = 2, 2 2/3' = 3, 2' = 4,
    // 1 3/5' = 5, 1 1/3' = 6, 1' = 8. Levels 0..8, like the real thing.
    static const uint8_t reg[ORGAN_CYCLES][7] = {
        {8, 0, 0, 0, 0, 0, 0},  // flute
        {8, 6, 0, 0, 0, 0, 0},
        {8, 8, 6, 0, 0, 0, 0},  // jazz
        {8, 8, 8, 6, 0, 0, 0},
        {8, 6, 8, 8, 4, 0, 0},  // gospel
        {8, 8, 8, 8, 6, 4, 0},
        {8, 8, 8, 8, 8, 6, 6},
        {8, 8, 8, 8, 8, 8, 8},  // full
    };
    static const int harm[7] = {1, 2, 3, 4, 5, 6, 8};
    for (int c = 0; c < ORGAN_CYCLES; c++) {
        float amp[9] = {0};
        for (int d = 0; d < 7; d++) amp[harm[d]] = reg[c][d] / 8.0f / (1.0f + 0.15f * d);  // upper bars a touch softer
        additive(amp, 8, out + c * TABLE_CYCLE);
    }
}

void tables_vox(int16_t *out) {
    // Formants (Hz) and their levels, for U O A E I.
    static const float F[VOX_CYCLES][3] = {
        {300, 870, 2240}, {570, 840, 2410}, {730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}};
    static const float L[3] = {1.0f, 0.6f, 0.25f};
    const float f0 = 196.0f;  // G3: the table's fundamental
    enum { N = 40 };
    for (int c = 0; c < VOX_CYCLES; c++) {
        float amp[N + 1] = {0};
        for (int h = 1; h <= N; h++) {
            const float f = h * f0;
            float a = 0.0f;
            for (int k = 0; k < 3; k++) {  // a resonance per formant
                const float bw = 60.0f + F[c][k] * 0.08f, x = (f - F[c][k]) / bw;
                a += L[k] / (1.0f + x * x);
            }
            amp[h] = a / sqrtf((float)h);  // a gentle downward tilt, like a voice
        }
        additive(amp, N, out + c * TABLE_CYCLE);
    }
}
