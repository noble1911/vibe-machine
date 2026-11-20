// synthedit.c -- see synthedit.h.

#include "synthedit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const kNames[ED_PARAMS] = {"ATTACK", "DECAY",  "SUSTAIN", "RELEASE", "CUTOFF", "RESO",
                                              "F.ENV",  "RATE",   "DEPTH",   "GLIDE",   "DETUNE", "BRIGHT"};
static const char *const kTargets[LFO_TARGETS] = {"PITCH", "FILTER", "VOLUME", "PAN"};

const char *edit_param_name(EditParam p) { return p >= 0 && p < ED_PARAMS ? kNames[p] : "?"; }
const char *lfo_target_name(int t) { return t >= 0 && t < LFO_TARGETS ? kTargets[t] : "?"; }

static float clamp01(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }  // NaN -> 0

void edit_reset(SynthEdit *e, const float defaults[ED_PARAMS]) {
    memset(e, 0, sizeof(*e));
    for (int i = 0; i < ED_PARAMS; i++) e->p[i] = clamp01(defaults[i]);
    e->unison = 1;
}

void edit_set(SynthEdit *e, EditParam p, float v) {
    if (p < 0 || p >= ED_PARAMS) return;
    e->p[p] = clamp01(v);
    e->set |= (uint16_t)(1u << p);
}

void edit_clamp(SynthEdit *e) {
    for (int i = 0; i < ED_PARAMS; i++) e->p[i] = clamp01(e->p[i]);
    e->set &= (uint16_t)((1u << ED_PARAMS) - 1);
    if (e->lfo_target >= LFO_TARGETS) e->lfo_target = LFO_PITCH;
    e->mono = e->mono ? 1 : 0;
    if (e->unison < 1) e->unison = 1;
    if (e->unison > UNISON_MAX) e->unison = UNISON_MAX;
}

void edit_morph(const SynthEdit *a, const SynthEdit *b, float t, SynthEdit *out) {
    t = clamp01(t);
    SynthEdit r = t < 0.5f ? *a : *b;  // the switches
    for (int i = 0; i < ED_PARAMS; i++) r.p[i] = a->p[i] + (b->p[i] - a->p[i]) * t;
    r.set = a->set | b->set;
    *out = r;
}

void edit_mutate(SynthEdit *e, float amount, uint32_t seed) {
    uint32_t x = seed | 1u;
    for (int i = 0; i < ED_PARAMS; i++) {
        if (i == ED_GLIDE || i == ED_DETUNE) continue;  // these change how it's played, not its colour
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const float r = ((x >> 8) / 16777216.0f) * 2.0f - 1.0f;  // -1..1
        edit_set(e, (EditParam)i, e->p[i] + r * amount);
    }
}

static float expo(float t, float lo, float hi) { return lo * powf(hi / lo, t); }

float edit_real(EditParam p, float v) {
    v = clamp01(v);
    switch (p) {
        case ED_ATTACK: return expo(v, 1.0f, 4000.0f);
        case ED_DECAY: return expo(v, 20.0f, 6000.0f);
        case ED_RELEASE: return expo(v, 10.0f, 6000.0f);
        case ED_CUTOFF: return expo(v, 60.0f, 16000.0f);
        case ED_RESO: return expo(v, 0.7f, 10.0f);
        case ED_FENV: return (v - 0.5f) * 12.0f;
        case ED_LFO_RATE: return expo(v, 0.1f, 20.0f);
        case ED_GLIDE: return v < 0.02f ? 0.0f : expo(v, 10.0f, 1500.0f);
        case ED_DETUNE: return v * 40.0f;
        case ED_BRIGHT: return expo(v, 0.25f, 4.0f);
        default: return v;  // SUSTAIN, DEPTH
    }
}

static float unexpo(float r, float lo, float hi) {
    if (r <= lo) return 0.0f;
    return clamp01(logf(r / lo) / logf(hi / lo));
}

float edit_norm(EditParam p, float r) {
    switch (p) {
        case ED_ATTACK: return unexpo(r, 1.0f, 4000.0f);
        case ED_DECAY: return unexpo(r, 20.0f, 6000.0f);
        case ED_RELEASE: return unexpo(r, 10.0f, 6000.0f);
        case ED_CUTOFF: return unexpo(r, 60.0f, 16000.0f);
        case ED_RESO: return unexpo(r, 0.7f, 10.0f);
        case ED_FENV: return clamp01(r / 12.0f + 0.5f);
        case ED_LFO_RATE: return unexpo(r, 0.1f, 20.0f);
        case ED_GLIDE: return r < 10.0f ? 0.0f : unexpo(r, 10.0f, 1500.0f);
        case ED_DETUNE: return clamp01(r / 40.0f);
        case ED_BRIGHT: return unexpo(r, 0.25f, 4.0f);
        default: return clamp01(r);
    }
}

void edit_label(EditParam p, float v, char *out, int len) {
    const float r = edit_real(p, v);
    switch (p) {
        case ED_ATTACK: case ED_DECAY: case ED_RELEASE: case ED_GLIDE:
            if (r < 1.0f) snprintf(out, (size_t)len, "OFF");
            else if (r < 1000.0f) snprintf(out, (size_t)len, "%dms", (int)(r + 0.5f));
            else snprintf(out, (size_t)len, "%.1fs", r / 1000.0f);
            break;
        case ED_CUTOFF: case ED_LFO_RATE:
            if (r >= 1000.0f) snprintf(out, (size_t)len, "%.1fk", r / 1000.0f);
            else if (r >= 10.0f) snprintf(out, (size_t)len, "%dHz", (int)(r + 0.5f));
            else snprintf(out, (size_t)len, "%.1fHz", r);
            break;
        case ED_RESO: snprintf(out, (size_t)len, "Q%.1f", r); break;
        case ED_FENV: snprintf(out, (size_t)len, "%+.1foct", r); break;
        case ED_DETUNE: snprintf(out, (size_t)len, "%dct", (int)(r + 0.5f)); break;
        case ED_BRIGHT: snprintf(out, (size_t)len, "x%.2f", r); break;
        default: snprintf(out, (size_t)len, "%d%%", (int)(v * 100.0f + 0.5f)); break;
    }
}
