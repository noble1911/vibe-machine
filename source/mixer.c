// mixer.c -- see mixer.h.

#include "mixer.h"

#include <math.h>
#include <string.h>

#include "amy.h"

// AMY's per-bus block buffers (amy.c). The hook adds each strip into the
// master bus's buffer before AMY runs the master's own effects.
extern SAMPLE **fbl[AMY_MAX_CORES];

// Written by the UI thread, read by the audio thread (single words; a
// block that sees half an update is harmless).
typedef struct {
    float gain[2];   // left / right, including the headroom make-up
    float send[2];   // ECHO, VERB: how much of the strip goes to each
    float drive;     // pre-gain into the clipper, or 0 = DRIVE off
    float step;      // CRUSH quantisation step, or 0 = off
    int rate;        // CRUSH sample-and-hold length (1 = every sample)
} StripDsp;
static volatile StripDsp s_dsp[MIX_STRIPS];
static volatile float s_peak[MIX_STRIPS];
static volatile float s_out_scale = 0.1f;  // AMY's mixdown: 0.1 x the master's volume
static float s_out[MIX_STRIPS][AMY_BLOCK_SIZE];  // audio thread only
// What the strips sent to the echo and reverb buses this block, to take
// back out after the effect (leaving only the wet sound). Audio thread only.
static SAMPLE s_sent[2][AMY_BLOCK_SIZE * AMY_NCHANS];
// Audio thread only.
static float s_hold[MIX_STRIPS][2];
static int s_count[MIX_STRIPS][2];

static const char *const kKnobNames[MIX_KNOBS] = {"DRIVE", "CRUSH", "TONE", "PAN", "ECHO", "VERB"};

const char *mixer_knob_name(MixKnob k) { return (k >= 0 && k < MIX_KNOBS) ? kKnobNames[k] : "?"; }

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

#define KNOB_OFF 0.02f  // DRIVE / CRUSH below this are bypassed

// Pre-gain into the clipper: starts where a typical part already touches the
// knee, so the first bit of the knob is audible.
static float drive_of(float d) { return 2.0f * exp2f(clamp01(d) * 3.0f); }  // 2..16

void mixer_fill_event(int strip, const MixStrip *s, struct amy_event *e) {
    float tone = clamp01(s->knob[MIX_TONE]);
    if (fabsf(tone - 0.5f) < 0.03f) tone = 0.5f;  // flat: AMY skips the EQ entirely
    e->bus = (uint16_t)strip;
    e->eq_l = (0.5f - tone) * 24.0f;  // the same tilt as the SOUND page's TONE
    e->eq_m = 0.0f;
    e->eq_h = (tone - 0.5f) * 24.0f;
}

void mixer_set_gain(int strip, const MixStrip *s, bool audible) {
    if (strip < 0 || strip >= MIX_STRIPS) return;
    const float d = clamp01(s->knob[MIX_DRIVE]), c = clamp01(s->knob[MIX_CRUSH]);
    float g = 0.0f;
    if (audible) {
        const float l = clamp01(s->level) / MIX_LEVEL_UNITY;
        g = l * l / MIX_HEADROOM;
        // Drive turns a part up into the clipper, and clipping squares the
        // wave off (louder for the same peak): give most of that back.
        // Measured in tests/mix_probe.c: within a few dB across the knob.
        if (d >= KNOB_OFF) g *= powf(drive_of(d), -0.6f);
    }
    const float p = clamp01(s->knob[MIX_PAN]);
    volatile StripDsp *x = &s_dsp[strip];
    x->drive = d >= KNOB_OFF ? drive_of(d) : 0.0f;
    // 12 bits down to 5 (fewer, and quiet parts vanish between the steps),
    // and the sample rate down to 1/15th (about 3 kHz).
    x->step = c >= KNOB_OFF ? exp2f(1.0f - (12.5f - c * 7.0f)) : 0.0f;
    x->rate = c >= KNOB_OFF ? 1 + (int)(c * 14.0f) : 1;
    x->send[0] = clamp01(s->knob[MIX_ECHO]);
    x->send[1] = clamp01(s->knob[MIX_VERB]);
    x->gain[0] = g * (p > 0.5f ? 2.0f * (1.0f - p) : 1.0f);
    x->gain[1] = g * (p < 0.5f ? 2.0f * p : 1.0f);
}

void mixer_hook(uint16_t bus, int32_t *buf, uint16_t len) {
    // All the buses must be live (AMY only clears and runs buses up to the
    // highest one in use).
    if (amy_global.highest_bus < MIX_VERB_BUS) return;
    if (bus == MIX_ECHO_BUS || bus == MIX_VERB_BUS) {  // the effect has run: keep only what it added
        const SAMPLE *sent = s_sent[bus - MIX_ECHO_BUS];
        for (int i = 0; i < len * AMY_NCHANS; i++) buf[i] -= sent[i];
        return;
    }
    if (bus >= MIX_STRIPS) return;  // the master: AMY mixes it down
    if (bus == 0) memset(s_sent, 0, sizeof(s_sent));  // the first strip of a block
    SAMPLE *master = fbl[0][MIX_MASTER_BUS];
    SAMPLE *to_fx[2] = {fbl[0][MIX_ECHO_BUS], fbl[0][MIX_VERB_BUS]};
    const float send[2] = {s_dsp[bus].send[0], s_dsp[bus].send[1]};
    const float drive = s_dsp[bus].drive, step = s_dsp[bus].step;
    const int rate = s_dsp[bus].rate;
    float peak = 0.0f;
    float *out = s_out[bus];
    const float out_scale = s_out_scale / AMY_NCHANS;
    for (int c = 0; c < AMY_NCHANS; c++) {
        const float g = s_dsp[bus].gain[c & 1];
        SAMPLE *src = buf + c * len, *dst = master + c * len;
        float hold = s_hold[bus][c & 1];
        int count = s_count[bus][c & 1];
        for (int i = 0; i < len; i++) {
            float x = S2F(src[i]);
            src[i] = 0;  // so AMY's own mixdown doesn't count it twice
            if (drive > 0.0f) {  // soft clip: cubic knee, flat at +-2/3
                float v = x * drive;
                v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
                x = v - v * v * v * (1.0f / 3.0f);
            }
            if (step > 0.0f) {  // crush: hold every `rate` samples, rounded to `step`
                if (--count <= 0) {
                    hold = floorf(x / step + 0.5f) * step;
                    count = rate;
                }
                x = hold;
            }
            x *= g;
            const SAMPLE sx = F2S(x);
            dst[i] += sx;
            for (int f = 0; f < 2; f++)  // the sends
                if (send[f] > 0.0f) {
                    const SAMPLE v = send[f] >= 1.0f ? sx : F2S(x * send[f]);
                    to_fx[f][c * len + i] += v;
                    s_sent[f][c * len + i] += v;
                }
            if (c == 0) out[i] = x * out_scale;  // the mono copy for the tape
            else out[i] += x * out_scale;
            const float a = fabsf(x);
            if (a > peak) peak = a;
        }
        s_hold[bus][c & 1] = hold;
        s_count[bus][c & 1] = count;
    }
    if (peak > s_peak[bus]) s_peak[bus] = peak;
}

void mixer_set_master_volume(float volume) { s_out_scale = 0.1f * volume; }

const float *mixer_strip_block(int strip) { return strip >= 0 && strip < MIX_STRIPS ? s_out[strip] : NULL; }

float mixer_peak(int strip) {
    if (strip < 0 || strip >= MIX_STRIPS) return 0.0f;
    float p = s_peak[strip];
    s_peak[strip] = 0.0f;
    return p;
}
