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

// The room kept between neighbouring cuts (and START / END).
static float cut_gap(const SampleEdit *e) {
    const float g = (e->end - e->start) / (e->slices * 8.0f);
    return g < 0.004f ? g : 0.004f;
}

void sampler_edit_even(SampleEdit *e) {
    for (int k = 0; k < SAMPLER_MAX_SLICES - 1; k++)
        e->cut[k] = k < e->slices - 1 ? e->start + (e->end - e->start) * (k + 1) / e->slices : 0.0f;
}

void sampler_edit_clamp(SampleEdit *e) {
    const float min_len = 0.02f;
    if (!(e->start >= 0.0f)) e->start = 0.0f;  // NaN too
    if (!(e->end <= 1.0f)) e->end = 1.0f;
    if (e->start > 1.0f - min_len) e->start = 1.0f - min_len;
    if (e->end < e->start + min_len) e->end = e->start + min_len;
    if (e->slices == 1 || e->slices > SAMPLER_MAX_SLICES) e->slices = 0;
    const int n = e->slices > 0 ? e->slices - 1 : 0;
    for (int k = 0; k < n; k++)
        if (!(e->cut[k] == e->cut[k])) {  // NaN: start again from equal slices
            sampler_edit_even(e);
            break;
        }
    const float gap = n ? cut_gap(e) : 0.0f;
    float lo = e->start + gap;  // forwards: each cut after the one before
    for (int k = 0; k < n; k++) {
        if (e->cut[k] < lo) e->cut[k] = lo;
        lo = e->cut[k] + gap;
    }
    float hi = e->end - gap;  // backwards: each cut before the one after
    for (int k = n - 1; k >= 0; k--) {
        if (e->cut[k] > hi) e->cut[k] = hi;
        hi = e->cut[k] - gap;
    }
    for (int k = n; k < SAMPLER_MAX_SLICES - 1; k++) e->cut[k] = 0.0f;
}

float sampler_bound(const SampleEdit *e, int b) {
    if (b <= 0 || e->slices <= 0) return e->start;
    if (b >= e->slices) return e->end;
    return e->cut[b - 1];
}

int sampler_region(const SampleEdit *e, int frames, int slice, int *first) {
    float fa = e->start, fb = e->end;
    if (slice >= 0 && e->slices > 0) {
        const int k = slice % e->slices;
        fa = sampler_bound(e, k);
        fb = sampler_bound(e, k + 1);
    }
    int a = (int)(fa * frames), b = (int)(fb * frames);
    if (a < 0) a = 0;
    if (b > frames) b = frames;
    if (b <= a) b = a + 1 <= frames ? a + 1 : a;
    *first = a;
    return b - a;
}

float sampler_snap(const int16_t *buf, int frames, float pos) {
    const int at = (int)(pos * frames), reach = SAMPLER_RATE / 400;  // 2.5 ms
    for (int d = 0; d <= reach; d++)
        for (int s = -1; s <= 1; s += 2) {
            const int i = at + d * s;
            if (i <= 0 || i >= frames) continue;
            if ((buf[i - 1] < 0) != (buf[i] < 0) || buf[i] == 0) return (i + 0.5f) / frames;
        }
    return pos;
}

int sampler_auto_chop(const int16_t *buf, int frames, SampleEdit *e) {
    const int want = e->slices - 1;
    if (want <= 0) return 0;
    if (!buf || frames <= 0) {
        sampler_edit_even(e);
        return 0;
    }
    // Level in 5 ms hops across the region.
    const int hop = SAMPLER_RATE / 200, a = (int)(e->start * frames), b = (int)(e->end * frames);
    const int hops = (b - a) / hop;
    if (hops < 8) {
        sampler_edit_even(e);
        return 0;
    }
    float *env = malloc(sizeof(float) * (size_t)hops * 2);
    if (!env) {
        sampler_edit_even(e);
        return 0;
    }
    float *onset = env + hops, peak = 0.0f;
    for (int h = 0; h < hops; h++) {
        double s = 0;
        for (int i = 0; i < hop; i++) s += abs(buf[a + h * hop + i]);
        env[h] = (float)(s / hop);
        peak = env[h] > peak ? env[h] : peak;
    }
    // An attack: the level jumps well above the last 20 ms, and it's loud
    // enough to matter.
    for (int h = 0; h < hops; h++) {
        float before = 0.0f;
        for (int j = 1; j <= 4; j++) before += h - j >= 0 ? env[h - j] : env[0];
        before /= 4.0f;
        const float rise = logf((env[h] + 30.0f) / (before + 30.0f));
        onset[h] = env[h] > peak * 0.08f && rise > 0.6f ? rise * sqrtf(env[h] / peak) : 0.0f;
    }
    // The strongest attacks, kept apart (and away from START, which is
    // already a boundary).
    const int apart = hops / (e->slices * 3) > 8 ? hops / (e->slices * 3) : 8;  // at least 40 ms
    int picked[SAMPLER_MAX_SLICES - 1], got = 0;
    while (got < want) {
        int best = -1;
        for (int h = apart; h < hops - 2; h++) {
            if (onset[h] <= 0.0f || (best >= 0 && onset[h] <= onset[best])) continue;
            bool clear = true;
            for (int k = 0; k < got && clear; k++) clear = abs(h - picked[k]) >= apart;
            if (clear) best = h;
        }
        if (best < 0) break;
        picked[got++] = best;
    }
    free(env);
    const int hits = got;
    // Just before each attack (2 ms), as fractions; then fill any missing
    // cuts by splitting the longest slice.
    float cuts[SAMPLER_MAX_SLICES - 1];
    for (int k = 0; k < got; k++) {
        const int f = a + picked[k] * hop - SAMPLER_RATE / 500;
        cuts[k] = sampler_snap(buf, frames, (float)f / frames);
    }
    while (got < want) {
        float bounds[SAMPLER_MAX_SLICES + 1];
        int nb = 0;
        bounds[nb++] = e->start;
        for (int k = 0; k < got; k++) bounds[nb++] = cuts[k];
        bounds[nb++] = e->end;
        for (int i = 1; i < nb; i++)  // sort (few)
            for (int j = i; j > 0 && bounds[j] < bounds[j - 1]; j--) {
                float t = bounds[j];
                bounds[j] = bounds[j - 1];
                bounds[j - 1] = t;
            }
        int widest = 0;
        for (int i = 1; i < nb - 1; i++)
            if (bounds[i + 1] - bounds[i] > bounds[widest + 1] - bounds[widest]) widest = i;
        cuts[got++] = sampler_snap(buf, frames, (bounds[widest] + bounds[widest + 1]) / 2.0f);
    }
    for (int i = 1; i < want; i++)
        for (int j = i; j > 0 && cuts[j] < cuts[j - 1]; j--) {
            float t = cuts[j];
            cuts[j] = cuts[j - 1];
            cuts[j - 1] = t;
        }
    for (int k = 0; k < SAMPLER_MAX_SLICES - 1; k++) e->cut[k] = k < want ? cuts[k] : 0.0f;
    sampler_edit_clamp(e);
    return hits;
}

int sampler_slice_of_pitch(int pitch, int slices) {
    if (slices <= 0) return 0;
    const int d = music_degree(pitch);  // the keys walk up the song's scale
    return ((d % slices) + slices) % slices;
}

int sampler_slice_note_moved(int pitch, int from_root, int from_scale) {
    const int p = music_degree_note(music_degree_in(pitch, from_root, from_scale));
    return p < 0 ? 0 : p > 127 ? 127 : p;
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
