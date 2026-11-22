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

// How long the hook took this block, all its calls together (for the perf
// log: read and cleared by mixer_hook_ticks()).
static uint32_t s_hook_ticks;
#if defined(__3DS__) && defined(VM_TUNE)
#include <3ds.h>
#define HOOK_NOW() svcGetSystemTick()
#else
#define HOOK_NOW() 0ull
#endif
uint32_t mixer_hook_ticks(void) {
    const uint32_t t = s_hook_ticks;
    s_hook_ticks = 0;
    return t;
}
static void mixer_hook_body(uint16_t bus, int32_t *buf, uint16_t len);
void mixer_hook(uint16_t bus, int32_t *buf, uint16_t len) {
    const unsigned long long t0 = HOOK_NOW();
    mixer_hook_body(bus, buf, len);
    s_hook_ticks += (uint32_t)(HOOK_NOW() - t0);
}

// ---- the echo and the reverb fall asleep ---------------------------------
// AMY runs a bus's echo / reverb on every block while its level is above 0,
// silence or not, and the two together cost about as much as five JUNO
// notes. So when nothing has been sent to one and its tail has died away,
// its level is held at 0 (AMY then skips it); the first strip that sends
// to it again wakes it within the same block. Only once the engine has said
// what the levels should be (mixer_set_fx_level): until then, hands off.
#define FX_QUIET 4096  // quieter than this (-66 dB on the bus, under the last bit of the output) is a dead tail: the reverb idles in a small limit cycle (about 600) and never reaches 0
#define FX_SLEEP_BLOCKS (2 * AMY_SAMPLE_RATE / AMY_BLOCK_SIZE)  // 2 s: longer than the longest delay line
static volatile float s_fx_want[2] = {-1.0f, -1.0f};
static bool s_fx_asleep[2], s_fx_fed[2], s_fx_dirty[2];  // audio thread only
// The strips add what they send into the echo's bus only: the reverb's bus
// is a copy of it, made once a block (see the master's turn in the hook).
// A strip whose two sends differ adds the difference here.
static SAMPLE s_delta[AMY_BLOCK_SIZE * AMY_NCHANS];
static bool s_delta_dirty, s_fx0_dirty;
static const SAMPLE *s_sentp[2] = {NULL, NULL};  // what each effect was sent this block
// Which strips keep a mono copy of themselves (mixer_strip_block()).
static volatile unsigned s_keep = ~0u;
#define STRIP_QUIET 1024  // a strip quieter than this is under the last bit of the output: a tone EQ's filters never ring down to 0
static int s_fx_quiet[2];

void mixer_set_fx_level(int fx, float level) {
    if (fx >= 0 && fx < 2) s_fx_want[fx] = level;
}
void mixer_keep_strips(unsigned mask) { s_keep = mask; }
bool mixer_fx_asleep(int fx) { return fx >= 0 && fx < 2 && s_fx_asleep[fx]; }

static SAMPLE *fx_level(int f) {
    return f == 0 ? &amy_global.bus[MIX_ECHO_BUS]->echo.level : &amy_global.bus[MIX_VERB_BUS]->reverb.level;
}
static void fx_wake(int f) {
    s_fx_fed[f] = true;
    if (!s_fx_asleep[f]) return;
    s_fx_asleep[f] = false;
    s_fx_quiet[f] = 0;
    const float want = s_fx_want[f];
    if (want >= 0.0f) *fx_level(f) = F2S(want);
}

static void mixer_hook_body(uint16_t bus, int32_t *buf, uint16_t len) {
    // All the buses must be live (AMY only clears and runs buses up to the
    // highest one in use).
    if (amy_global.highest_bus < MIX_VERB_BUS) return;
    if (bus == MIX_ECHO_BUS || bus == MIX_VERB_BUS) {  // the effect has run: keep only what it added
        const int f = bus - MIX_ECHO_BUS;
        if (s_fx_asleep[f]) return;  // (nothing went in, nothing ran: the bus is silent)
        const SAMPLE *sent = s_sentp[f];
        int32_t loud = 0;
        if (s_fx_dirty[f] && sent) {
            for (int i = 0; i < len * AMY_NCHANS; i++) {
                const int32_t x = buf[i] - sent[i];
                buf[i] = x;
                loud |= x ^ (x >> 31);
            }
        } else {
            for (int i = 0; i < len * AMY_NCHANS; i++) loud |= buf[i] ^ (buf[i] >> 31);
        }
        const float want = s_fx_want[f];
        if (s_fx_fed[f] || loud >= FX_QUIET || want < 0.0f) s_fx_quiet[f] = 0;
        else if (*fx_level(f) > 0 && ++s_fx_quiet[f] >= (int)FX_SLEEP_BLOCKS) {
            s_fx_asleep[f] = true;
            *fx_level(f) = 0;
        }
        return;
    }
    if (bus == MIX_MASTER_BUS) {
        // Every strip has been through. The echo's bus holds what they sent
        // (nothing else plays into it): the reverb's is the same, give or
        // take the strips whose two sends differ. Note both down before the
        // effects run, to take the dry sound back out afterwards.
        const int n = len * AMY_NCHANS;
        SAMPLE *echo = fbl[0][MIX_ECHO_BUS], *verb = fbl[0][MIX_VERB_BUS];
        s_sentp[0] = s_sentp[1] = NULL;
        if (s_fx0_dirty) {
            memcpy(s_sent[0], echo, sizeof(SAMPLE) * n);
            s_sentp[0] = s_sent[0];
        }
        if (s_fx_dirty[1]) {
            if (s_fx0_dirty && !s_delta_dirty) {
                memcpy(verb, echo, sizeof(SAMPLE) * n);
                s_sentp[1] = s_sent[0];
            } else {
                if (s_fx0_dirty)
                    for (int i = 0; i < n; i++) verb[i] = s_sent[1][i] = echo[i] + s_delta[i];
                else
                    for (int i = 0; i < n; i++) verb[i] = s_sent[1][i] = s_delta[i];
                s_sentp[1] = s_sent[1];
            }
        }
        if (s_delta_dirty) memset(s_delta, 0, sizeof(s_delta));
        return;
    }
    if (bus >= MIX_STRIPS) return;
    if (bus == 0) {  // the first strip of a block
        for (int f = 0; f < 2; f++) {
            s_fx_dirty[f] = s_fx_fed[f] = false;
            s_fx0_dirty = s_delta_dirty = false;
            // The level the engine asked for (or 0, asleep). A level of 0
            // from the engine needs no sleep: AMY skips it anyway.
            const float want = s_fx_want[f];
            if (want >= 0.0f && amy_global.bus[MIX_ECHO_BUS + f] != NULL) *fx_level(f) = s_fx_asleep[f] ? 0 : F2S(want);
        }
    }
    const int n = len * AMY_NCHANS;
    {   // A silent strip (nothing playing on it, crush not holding a value):
        // nothing to add anywhere.
        int32_t any = 0;
        for (int i = 0; i < n; i++) any |= buf[i] ^ (buf[i] >> 31);
        if (any < STRIP_QUIET && s_hold[bus][0] == 0.0f && s_hold[bus][1] == 0.0f) {
            if (any) memset(buf, 0, sizeof(SAMPLE) * n);
            memset(s_out[bus], 0, sizeof(float) * len);
            return;
        }
    }
    SAMPLE *master = fbl[0][MIX_MASTER_BUS];
    SAMPLE *const echo = fbl[0][MIX_ECHO_BUS];
    const float send[2] = {s_dsp[bus].send[0], s_dsp[bus].send[1]};
    const float drive = s_dsp[bus].drive, step = s_dsp[bus].step;
    const int rate = s_dsp[bus].rate;
    float *out = s_out[bus];
    const float out_scale = s_out_scale / AMY_NCHANS;
    for (int f = 0; f < 2; f++)
        if (send[f] > 0.0f) {
            fx_wake(f);
            s_fx_dirty[f] = true;
        }
    // The sends, in 16.16: all of send[0] goes to the echo's bus, and the
    // reverb's bus gets that plus (send[1] - send[0]) from s_delta.
    const int32_t sg0 = (int32_t)(send[0] * 65536.0f), sgd = (int32_t)(send[1] * 65536.0f) - sg0;
    if (sg0) s_fx0_dirty = true;
    if (sgd) s_delta_dirty = true;
    const bool keep = (s_keep >> bus) & 1;

    if (drive <= 0.0f && step <= 0.0f) {
        // The usual strip: only a gain, a pan and the sends. All in whole
        // numbers (one 64-bit multiply a sample), and touching as little
        // memory as it can: on the 3DS the cost here is cache misses, a
        // few per buffer per pass, far more than the arithmetic.
        int32_t peak = 0;
        int32_t first[AMY_BLOCK_SIZE];  // the first channel, for the mono copy
        const float k = out_scale / (float)(1 << S_FRAC_BITS);
        for (int c = 0; c < AMY_NCHANS; c++) {
            const int32_t g = (int32_t)(s_dsp[bus].gain[c & 1] * 65536.0f);
            const SAMPLE *restrict src = buf + c * len;
            SAMPLE *restrict dst = master + c * len;
            SAMPLE *restrict fx = echo + c * len;
            if (sg0 >= 65536 && !sgd && !keep) {  // how a strip starts out: full sends, no tape copy
                for (int i = 0; i < len; i++) {
                    const int32_t sx = (int32_t)(((int64_t)src[i] * g) >> 16);
                    dst[i] += sx;
                    fx[i] += sx;
                    const int32_t mag = sx ^ (sx >> 31);
                    if (mag > peak) peak = mag;
                }
                continue;
            }
            SAMPLE *restrict dl = s_delta + c * len;
            for (int i = 0; i < len; i++) {
                const int32_t sx = (int32_t)(((int64_t)src[i] * g) >> 16);
                dst[i] += sx;
                if (sg0) fx[i] += sg0 >= 65536 ? sx : (int32_t)(((int64_t)sx * sg0) >> 16);
                if (sgd) dl[i] += (int32_t)(((int64_t)sx * sgd) >> 16);
                const int32_t mag = sx ^ (sx >> 31);
                if (mag > peak) peak = mag;
                if (keep) {
                    if (AMY_NCHANS == 1) out[i] = (float)sx * k;
                    else if (c == 0) first[i] = sx;
                    else out[i] = (float)(first[i] + sx) * k;
                }
            }
        }
        memset(buf, 0, sizeof(SAMPLE) * n);  // so AMY's own mixdown doesn't count it twice
        const float pk = S2F(peak);
        if (pk > s_peak[bus]) s_peak[bus] = pk;
        return;
    }

    float peak = 0.0f;
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
            if (sg0) echo[c * len + i] += sg0 >= 65536 ? sx : (int32_t)(((int64_t)sx * sg0) >> 16);  // the sends
            if (sgd) s_delta[c * len + i] += (int32_t)(((int64_t)sx * sgd) >> 16);
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
