// sampler.c -- see sampler.h.

#include "sampler.h"

#include "music.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int sampler_tidy(int16_t *buf, int frames) {
    if (frames <= 0) return 0;
    int peak = 0;
    for (int i = 0; i < frames; i++) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
    if (peak < 300) return 0;  // essentially silence (about -40 dBFS)

    // Start a hair before the first sound above 6% of the peak, so the
    // note speaks the moment a key is pressed.
    const int thresh = peak * 6 / 100, pre = SAMPLER_RATE / 200;  // 5 ms
    int start = 0;
    while (start < frames && abs(buf[start]) < thresh) start++;
    start = start > pre ? start - pre : 0;
    int end = frames;
    while (end > start + 1 && abs(buf[end - 1]) < thresh / 2) end--;
    int n = end - start;
    memmove(buf, buf + start, (size_t)n * sizeof(int16_t));

    const float gain = 0.9f * 32767.0f / (float)peak;
    const int fade = SAMPLER_RATE / 200 < n / 4 ? SAMPLER_RATE / 200 : n / 4;
    for (int i = 0; i < n; i++) {
        float g = gain;
        if (i < fade) g *= (float)i / fade;
        if (n - 1 - i < fade) g *= (float)(n - 1 - i) / fade;
        float v = buf[i] * g;
        buf[i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
    }
    return n;
}

void sampler_overview(const int16_t *buf, int frames, float *out, int points) {
    for (int p = 0; p < points; p++) {
        int a = (int)((long)frames * p / points), b = (int)((long)frames * (p + 1) / points);
        int peak = 0;
        for (int i = a; i < b; i++) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
        out[p] = peak / 32768.0f;
    }
}

int sampler_boing(int16_t *buf, int max_frames) {
    const int n = SAMPLER_RATE * 6 / 10 < max_frames ? SAMPLER_RATE * 6 / 10 : max_frames;
    double phase = 0.0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLER_RATE;
        // Pitch springs down from a fifth above C4 with a wobble; the level decays.
        double f = 261.63 * (1.0 + 0.5 * exp(-t * 9.0)) * (1.0 + 0.06 * sin(2 * M_PI * 11.0 * t) * exp(-t * 3.0));
        phase += 2 * M_PI * f / SAMPLER_RATE;
        double v = sin(phase) + 0.3 * sin(2 * phase) + 0.15 * sin(3 * phase);
        buf[i] = (int16_t)(v / 1.45 * exp(-t * 4.0) * 0.9 * 32767.0);
    }
    return n;
}

void sampler_edit_clamp(SampleEdit *e) {
    const float min_len = 0.02f;
    if (!(e->start >= 0.0f)) e->start = 0.0f;  // NaN too
    if (!(e->end <= 1.0f)) e->end = 1.0f;
    if (e->start > 1.0f - min_len) e->start = 1.0f - min_len;
    if (e->end < e->start + min_len) e->end = e->start + min_len;
    if (e->slices == 1 || e->slices > SAMPLER_MAX_SLICES) e->slices = 0;
}

int sampler_region(const SampleEdit *e, int frames, int slice, int *first) {
    int a = (int)(e->start * frames), b = (int)(e->end * frames);
    if (a < 0) a = 0;
    if (b > frames) b = frames;
    if (b <= a) b = a + 1 <= frames ? a + 1 : a;
    if (slice >= 0 && e->slices > 0) {
        const int n = b - a, k = slice % e->slices;
        const int sa = a + (int)((long)n * k / e->slices), sb = a + (int)((long)n * (k + 1) / e->slices);
        a = sa;
        b = sb;
    }
    *first = a;
    return b - a;
}

int sampler_slice_of_pitch(int pitch, int slices) {
    if (slices <= 0) return 0;
    const int d = music_degree(pitch);  // the keys walk up the song's scale
    return ((d % slices) + slices) % slices;
}

int sampler_resample(const int16_t *in, int n, int in_rate, int16_t *out, int max_out, int out_rate) {
    if (n <= 0 || in_rate <= 0 || out_rate <= 0) return 0;
    if (in_rate == out_rate) {
        int m = n < max_out ? n : max_out;
        memmove(out, in, (size_t)m * sizeof(int16_t));
        return m;
    }
    const double step = (double)in_rate / out_rate;
    int m = (int)((n - 1) / step) + 1;
    if (m > max_out) m = max_out;
    for (int i = 0; i < m; i++) {
        double pos = i * step;
        int j = (int)pos;
        double f = pos - j;
        int a = in[j], b = j + 1 < n ? in[j + 1] : in[j];
        out[i] = (int16_t)lrint(a + (b - a) * f);
    }
    return m;
}

void sampler_normalise(int16_t *buf, int frames) {
    int peak = 0;
    for (int i = 0; i < frames; i++) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
    if (peak < 300) return;
    const float gain = 0.9f * 32767.0f / (float)peak;
    const int fade = SAMPLER_RATE / 1000 < frames / 4 ? SAMPLER_RATE / 1000 : frames / 4;  // 1 ms
    for (int i = 0; i < frames; i++) {
        float g = gain;
        if (i < fade) g *= (float)i / fade;
        if (frames - 1 - i < fade) g *= (float)(frames - 1 - i) / fade;
        float v = buf[i] * g;
        buf[i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
    }
}
