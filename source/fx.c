// fx.c -- see fx.h.
//
// Signal path per sample: SWEEP (filter) -> CRUSH -> history buffer ->
// STUTTER or TAPE (both read back from the history) -> out. Every effect
// fades in and out over ~10 ms so switching never clicks.

#include "fx.h"

#include <math.h>
#include <string.h>

#define HIST_FRAMES (1 << 17)  // ~3 s at 44.1 kHz
#define HIST_MASK (HIST_FRAMES - 1)
#define PI_F 3.14159265f

typedef struct {
    volatile bool on;
    volatile float x, y;
    float mix;  // 0..1, slewed towards on
} Param;

static Param s_p[FX_COUNT];
static float s_rate = 44100.0f;
static volatile float s_bpm = 112.0f;
static float s_slew;

static int16_t s_hist[HIST_FRAMES][2];
static uint32_t s_w;  // history write head

// SWEEP: Chamberlin state-variable filter, one per channel.
static float s_low[2], s_band[2];

// CRUSH: sample-and-hold state.
static float s_hold[2];
static int s_hold_count;

// STUTTER
static bool s_stut_active;
static uint32_t s_loop_start, s_loop_len, s_loop_pos;

// TAPE
static bool s_tape_active;
static double s_tape_rp;
static float s_tape_speed = 1.0f, s_tape_phase;
static float s_tape_wet;  // own crossfade: stays wet until back up to speed
static volatile float s_tape_speed_pub = 1.0f;

static const char *const kNames[FX_COUNT] = {"CRUSH", "SWEEP", "STUTTER", "TAPE"};
static const char *const kAxes[FX_COUNT][2] = {
    {"RATE", "BITS"}, {"LOW / HIGH", "RESO"}, {"LENGTH", "GATE"}, {"STOP", "WOBBLE"}};

void fx_init(int sample_rate) {
    s_rate = (float)sample_rate;
    s_slew = 1.0f / (0.01f * s_rate);
    memset(s_p, 0, sizeof(s_p));
    memset(s_hist, 0, sizeof(s_hist));
    s_w = 0;
}

void fx_set(FxKind k, bool on, float x, float y) {
    if (k < 0 || k >= FX_COUNT) return;
    s_p[k].x = x < 0 ? 0 : x > 1 ? 1 : x;
    s_p[k].y = y < 0 ? 0 : y > 1 ? 1 : y;
    s_p[k].on = on;
}

void fx_set_tempo(float bpm) { s_bpm = bpm > 20 ? bpm : 20; }

FxState fx_state(void) {
    FxState st;
    for (int k = 0; k < FX_COUNT; k++) {
        st.on[k] = s_p[k].on;
        st.x[k] = s_p[k].x;
        st.y[k] = s_p[k].y;
    }
    st.tape_speed = s_tape_speed_pub;
    return st;
}

const char *fx_name(FxKind k) { return (k >= 0 && k < FX_COUNT) ? kNames[k] : "?"; }

const char *fx_axis_name(FxKind k, int axis) {
    return (k >= 0 && k < FX_COUNT && axis >= 0 && axis < 2) ? kAxes[k][axis] : "";
}

static float slew(Param *p) {
    float target = p->on ? 1.0f : 0.0f;
    if (p->mix < target) p->mix = fminf(target, p->mix + s_slew);
    if (p->mix > target) p->mix = fmaxf(target, p->mix - s_slew);
    return p->mix;
}

static float clip(float v) { return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v; }

static void sweep(float io[2], float mix) {
    const float x = s_p[FX_SWEEP].x, y = s_p[FX_SWEEP].y;
    // Centre of the pad is a dead zone; left is low-pass, right is high-pass.
    bool high = x > 0.5f;
    float amt = high ? (x - 0.52f) / 0.48f : (0.48f - x) / 0.48f;
    if (amt < 0.0f) amt = 0.0f;
    float fc = high ? 30.0f * powf(200.0f, amt) : 6000.0f * powf(1.0f / 50.0f, amt);
    float f = 2.0f * sinf(PI_F * fc / s_rate);
    float q = 1.0f / (0.7f + y * 5.3f);
    for (int c = 0; c < 2; c++) {
        float hp = io[c] - s_low[c] - q * s_band[c];
        s_band[c] += f * hp;
        s_low[c] += f * s_band[c];
        float wet = high ? hp : s_low[c];
        if (amt <= 0.0f) wet = io[c];
        io[c] = io[c] + (clip(wet) - io[c]) * mix;
    }
}

static void crush(float io[2], float mix) {
    const int hold = 1 + (int)(s_p[FX_CRUSH].x * 31.0f);
    const float levels = powf(2.0f, 15.0f - s_p[FX_CRUSH].y * 13.0f);
    if (++s_hold_count >= hold) {
        s_hold_count = 0;
        for (int c = 0; c < 2; c++) s_hold[c] = roundf(io[c] * levels) / levels;
    }
    for (int c = 0; c < 2; c++) io[c] += (s_hold[c] - io[c]) * mix;
}

static uint32_t stutter_len(void) {
    // X picks the repeat: 1/2, 1/4, 1/8, 1/16 or 1/32 note at the tempo.
    static const float notes[] = {2.0f, 1.0f, 0.5f, 0.25f, 0.125f};
    int i = (int)(s_p[FX_STUTTER].x * 4.99f);
    float beat = s_rate * 60.0f / s_bpm;
    uint32_t len = (uint32_t)(beat * notes[i]);
    return len < 256 ? 256 : len > HIST_FRAMES / 2 ? HIST_FRAMES / 2 : len;
}

static void stutter_read(float out[2]) {
    const uint32_t fade = 64;
    uint32_t idx = (s_loop_start + s_loop_pos) & HIST_MASK;
    float g = 1.0f;
    // Gate: Y chops each repeat shorter, down to a quarter.
    uint32_t open = (uint32_t)(s_loop_len * (1.0f - 0.75f * s_p[FX_STUTTER].y));
    if (s_loop_pos >= open) g = 0.0f;
    else if (s_loop_pos < fade) g = (float)s_loop_pos / fade;
    else if (open - s_loop_pos < fade) g = (float)(open - s_loop_pos) / fade;
    out[0] = s_hist[idx][0] / 32768.0f * g;
    out[1] = s_hist[idx][1] / 32768.0f * g;
    if (++s_loop_pos >= s_loop_len) {
        s_loop_pos = 0;
        uint32_t want = stutter_len();  // follow X at loop boundaries
        if (want != s_loop_len) {
            s_loop_len = want;
            s_loop_start = (s_w - want) & HIST_MASK;
        }
    }
}

static void tape_read(float out[2]) {
    const float stop_secs = 0.15f + s_p[FX_TAPE].x * 1.85f;
    if (s_p[FX_TAPE].on) {
        s_tape_speed = fmaxf(0.0f, s_tape_speed - 1.0f / (stop_secs * s_rate));
    } else {
        s_tape_speed = fminf(1.0f, s_tape_speed + 1.0f / (0.12f * s_rate));  // spin back up
    }
    s_tape_phase += 6.0f / s_rate;
    if (s_tape_phase > 1.0f) s_tape_phase -= 1.0f;
    float wobble = 1.0f + s_p[FX_TAPE].y * 0.25f * sinf(2.0f * PI_F * s_tape_phase);
    s_tape_rp += s_tape_speed * wobble;
    double rp = s_tape_rp;
    uint32_t i0 = (uint32_t)rp & HIST_MASK, i1 = (i0 + 1) & HIST_MASK;
    float frac = (float)(rp - floor(rp));
    // A stopping tape fades out with its pitch (and never parks on a DC
    // value: the last sample held forever would be a thump, not silence).
    float gain = s_tape_speed < 0.3f ? s_tape_speed / 0.3f : 1.0f;
    for (int c = 0; c < 2; c++)
        out[c] = (s_hist[i0][c] * (1.0f - frac) + s_hist[i1][c] * frac) / 32768.0f * gain;
}

static bool idle(void) {
    for (int k = 0; k < FX_COUNT; k++)
        if (s_p[k].on || s_p[k].mix > 0.0f) return false;
    return !s_stut_active && !s_tape_active;
}

void fx_process(int16_t *io, int frames) {
    if (idle()) {
        // Nothing engaged: audio passes untouched; just keep the history fed.
        for (int n = 0; n < frames; n++) {
            s_hist[s_w][0] = io[2 * n];
            s_hist[s_w][1] = io[2 * n + 1];
            s_w = (s_w + 1) & HIST_MASK;
        }
        s_tape_speed_pub = 1.0f;
        return;
    }
    for (int n = 0; n < frames; n++) {
        float s[2] = {io[2 * n] / 32768.0f, io[2 * n + 1] / 32768.0f};

        float m = slew(&s_p[FX_SWEEP]);
        if (m > 0.0f) sweep(s, m);
        m = slew(&s_p[FX_CRUSH]);
        if (m > 0.0f) crush(s, m);

        // Freeze the history while stuttering so the loop stays intact.
        if (!s_stut_active) {
            s_hist[s_w][0] = (int16_t)(clip(s[0]) * 32767.0f);
            s_hist[s_w][1] = (int16_t)(clip(s[1]) * 32767.0f);
            s_w = (s_w + 1) & HIST_MASK;
        }

        float stut_mix = slew(&s_p[FX_STUTTER]);
        if (s_p[FX_STUTTER].on && !s_stut_active) {
            s_stut_active = true;
            s_loop_len = stutter_len();
            s_loop_start = (s_w - s_loop_len) & HIST_MASK;
            s_loop_pos = 0;
        }
        if (s_stut_active) {
            float r[2];
            stutter_read(r);
            for (int c = 0; c < 2; c++) s[c] += (r[c] - s[c]) * stut_mix;
            if (!s_p[FX_STUTTER].on && stut_mix <= 0.0f) s_stut_active = false;
        }

        slew(&s_p[FX_TAPE]);
        if (s_p[FX_TAPE].on && !s_tape_active) {
            s_tape_active = true;
            s_tape_speed = 1.0f;
            s_tape_rp = (double)((s_w - 1) & HIST_MASK);
        }
        if (s_tape_active) {
            float r[2];
            tape_read(r);
            // Stay wet until the tape is back up to speed, then fade to live.
            bool wet = s_p[FX_TAPE].on || s_tape_speed < 1.0f;
            s_tape_wet = wet ? fminf(1.0f, s_tape_wet + s_slew) : fmaxf(0.0f, s_tape_wet - s_slew);
            for (int c = 0; c < 2; c++) s[c] += (r[c] - s[c]) * s_tape_wet;
            if (!wet && s_tape_wet <= 0.0f) s_tape_active = false;
            // Keep the read head from drifting out of the buffer.
            if (s_tape_rp >= HIST_FRAMES) s_tape_rp -= HIST_FRAMES;
        }

        io[2 * n] = (int16_t)(clip(s[0]) * 32767.0f);
        io[2 * n + 1] = (int16_t)(clip(s[1]) * 32767.0f);
    }
    s_tape_speed_pub = s_tape_active ? s_tape_speed : 1.0f;
}
