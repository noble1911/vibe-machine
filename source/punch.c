// punch.c -- see punch.h.
//
// One ring, H, keeps what the repeaters put out (or the input, while none
// is on). A repeater's first cycle builds something out of H -- the audio
// still to come (LOOP), the beat just gone backwards (REVERSE), the bar
// just gone in a new order (SHUFFLE), a scratch over the last 16th -- and
// from then on it plays H one cycle back: a delay with full feedback. So a
// repeat is bit-exact, the ring never has to be frozen, and the seam where
// the first cycle starts again is crossfaded once and then just copied.
//
// Pitch (OCT, RISER) reads H through two crossfading taps whose delays
// slide (the usual delay-line shifter, a few tens of ms long). LO-FI reads
// its own small ring with a wobbling delay. The rest are filters, a gain
// and an echo. Cost per sample with nothing on: one copy into H and L.

#include "punch.h"

#include <math.h>
#include <string.h>

#define H_FRAMES (1 << 17)  // 4 s at 32.7 kHz (SHUFFLE gets a whole bar down to ~75 BPM)
#define H_MASK (H_FRAMES - 1)
#define L_FRAMES 2048  // LO-FI's ring (its delay wobbles inside ~20 ms)
#define L_MASK (L_FRAMES - 1)
#define E_FRAMES (1 << 15)  // DUB's echo: a dotted 1/8 down to ~45 BPM
#define E_MASK (E_FRAMES - 1)

static const char *const kNames[PUNCH_COUNT] = {
    "LOOP 1/4", "LOOP 1/8", "LOOP 1/16", "LOOP 1/32", "TRIPLET", "SHUFFLE", "REVERSE", "SCRATCH",
    "OCTAVE UP", "OCTAVE DOWN", "RISER", "LO-FI", "GATE", "KILL LO", "KILL HI", "DUB THROW"};
static const char *const kLabels[PUNCH_COUNT] = {
    "1/4", "1/8", "1/16", "1/32", "TRIPLET", "SHUFFLE", "REVERSE", "SCRATCH",
    "OCT UP", "OCT DN", "RISER", "LO-FI", "GATE", "KILL LO", "KILL HI", "DUB"};
static const char *const kAbout[PUNCH_COUNT] = {
    "repeats a beat", "repeats an 8th", "repeats a 16th", "rolls 32nds", "repeats in 3s against the beat",
    "the last bar's 16ths, reordered", "the last beat, backwards", "pulls the record back and forth",
    "everything an octave up", "everything an octave down", "pitch and filter climb for a bar",
    "wobbly old tape, dust and crackle", "chops to a pattern", "cuts the bass", "cuts the highs",
    "throws into an echo that rings on"};

const char *punch_name(PunchKind k) { return (k >= 0 && k < PUNCH_COUNT) ? kNames[k] : "?"; }
const char *punch_label(PunchKind k) { return (k >= 0 && k < PUNCH_COUNT) ? kLabels[k] : "?"; }
const char *punch_about(PunchKind k) { return (k >= 0 && k < PUNCH_COUNT) ? kAbout[k] : ""; }

// ------------------------------------------------------------- state ----

static int16_t s_h[H_FRAMES][2];
static int16_t s_l[L_FRAMES][2];
static int16_t s_e[E_FRAMES][2];
static uint32_t s_t;  // the frame being processed (H is indexed by it)
static float s_rate = 44100.0f;
static uint32_t s_fade;  // crossfade length, frames
static float s_slew;     // 1 / s_fade

// The clock.
static volatile uint16_t s_live;
static uint32_t s_tap;  // pads pressed since the last tick (atomic: UI ORs, audio takes)
static uint16_t s_pat, s_active;
static float s_bpm = 112.0f;
static double s_since;  // frames since the last tick
static int s_step = -1, s_step16;
static bool s_playing;

// The repeater (stage A). A voice is what plays; a LOOP waits as `pending`
// through its first cycle (the old voice carries on, being recorded), then
// takes over.
enum { V_DRY, V_LOOP, V_REV, V_SHUF, V_SCR };
typedef struct {
    uint8_t kind;
    int8_t fx;       // the PunchKind it plays, -1 for dry
    uint32_t e;      // frame it engaged on
    uint32_t len;    // LOOP: its length; REV: a beat; SHUF: a 16th; SCR: a beat
    uint32_t cyc;    // its cycle (REV / SCR: len; SHUF: n * len)
    uint32_t depth;  // SCR: how far back the hand pulls
    uint8_t n, perm[16];  // SHUF
} Voice;
static Voice s_cur, s_prev, s_pend;
static uint32_t s_xf;  // frames into the crossfade from s_prev to s_cur (>= s_fade: done)
static uint32_t s_rng = 0x2545F491u;

// Pitch (stage B).
static float s_pmix, s_phi, s_ratio = 1.0f, s_win;
static uint32_t s_riser_e;
static float s_riser;

// LO-FI.
static float s_lmix, s_wow, s_flut, s_ltone[2], s_lhp[2], s_lhpx[2], s_pop;
// KILL LO / KILL HI / the RISER's high-pass: two-pole state-variable filters.
typedef struct {
    float low, band;
} Svf;
static float s_kl_mix, s_kh_mix, s_rf_mix;
static Svf s_kl[2][2], s_kh[2][2], s_rf[2];
static float s_kl_f, s_kh_f, s_rf_f;
// GATE.
static float s_gmix, s_g = 1.0f;
// DUB.
static float s_dsend;
static bool s_dub_on;
static uint32_t s_dub_d, s_dub_win;
static float s_dub_peak, s_dub_lvl, s_dfb[2], s_dhp[2], s_dhpx[2];

static volatile PunchState s_pub;

// ----------------------------------------------------------- helpers ----

static inline float q15(int16_t v) { return v * (1.0f / 32768.0f); }
static inline int16_t to16(float v) {  // (rounds with a cast: no library call on the ARM11)
    const float s = v * 32768.0f;
    if (s >= 32767.0f) return 32767;
    if (s <= -32768.0f) return -32768;
    return (int16_t)(int)(s + (s >= 0.0f ? 0.5f : -0.5f));
}
static inline void hread(uint32_t i, float o[2]) {
    const int16_t *p = s_h[i & H_MASK];
    o[0] = q15(p[0]);
    o[1] = q15(p[1]);
}
// H at `back` frames (fractional) before frame `at`.
static inline void hback(uint32_t at, float back, float o[2]) {
    const uint32_t i = (uint32_t)back;
    const float f = back - (float)i;
    const int16_t *a = s_h[(at - i) & H_MASK], *b = s_h[(at - i - 1) & H_MASK];
    o[0] = q15(a[0]) + (q15(b[0]) - q15(a[0])) * f;
    o[1] = q15(a[1]) + (q15(b[1]) - q15(a[1])) * f;
}
static inline void mix2(float o[2], const float a[2], const float b[2], float t) {
    o[0] = a[0] + (b[0] - a[0]) * t;
    o[1] = a[1] + (b[1] - a[1]) * t;
}
// sin(2 pi x) for x >= 0, to about 0.1%: cheap enough for every sample.
static inline float fsin(float x) {
    x -= (float)(int)(x + 0.5f);  // -0.5..0.5
    float y = 8.0f * x - 16.0f * x * fabsf(x);
    return 0.225f * (y * fabsf(y) - y) + y;
}
static inline float fcos(float x) { return fsin(x + 0.25f); }
static uint32_t rng(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static float slew_to(float v, bool on) { return on ? fminf(1.0f, v + s_slew) : fmaxf(0.0f, v - s_slew); }
static float svf_f(float hz) { return 2.0f * sinf(3.14159265f * hz / s_rate); }
// One two-pole state-variable stage: returns the low-pass, or the high-pass.
static inline float svf(Svf *s, float x, float f, bool high) {
    const float hp = x - s->low - 1.414f * s->band;
    s->band += f * hp;
    s->low += f * s->band;
    return high ? hp : s->low;
}

static float beat_frames(void) { return s_rate * 60.0f / s_bpm; }
static float sixteenth(void) { return beat_frames() * 0.25f; }

// ----------------------------------------------------------- voices ----

// A repeater's first cycle, j frames in.
static void first(const Voice *v, uint32_t j, float o[2]) {
    switch (v->kind) {
        case V_REV: hread(v->e - 1 - j, o); return;
        case V_SHUF: {
            const uint32_t k = j / v->len, r = j - k * v->len, base = v->e - v->cyc;
            hread(base + v->perm[k] * v->len + r, o);
            if (r < s_fade && k > 0 && v->perm[k] != v->perm[k - 1] + 1) {
                float c[2];  // the slice before carries on under the fade
                hread(base + (v->perm[k - 1] + 1) * v->len + r, c);
                mix2(o, c, o, (float)r * s_slew);
            }
            return;
        }
        case V_SCR: {
            // The hand: at the record's position now, pulled back a 16th and
            // pushed forward again, twice a beat. Quiet where it turns round.
            const float ph = (float)j / (float)v->len;
            const float back = v->depth * 0.5f * (1.0f - fcos(2.0f * ph));
            const float speed = v->depth * 6.2831853f / (float)v->len * fabsf(fsin(2.0f * ph));
            hback(v->e - 1, back, o);
            const float g = fminf(1.0f, speed * 2.5f);
            o[0] *= g;
            o[1] *= g;
            return;
        }
        default: o[0] = o[1] = 0.0f; return;
    }
}

// What voice `v` plays at frame t (`in` is the input there).
static void pure(const Voice *v, uint32_t t, const float in[2], float o[2]) {
    if (v->kind == V_DRY) {
        o[0] = in[0];
        o[1] = in[1];
        return;
    }
    if (v->kind == V_LOOP) {
        hread(t - v->len, o);
        return;
    }
    const uint32_t j = t - v->e;
    if (j < v->cyc) {
        first(v, j, o);
    } else if (j < v->cyc + s_fade) {  // the seam: the first cycle begins again
        float a[2], b[2];
        first(v, j - v->cyc, b);
        if (v->kind == V_REV) {
            hread(v->e - 1 - j, a);  // (the reverse carries on under the fade)
        } else if (v->kind == V_SHUF) {
            hread(v->e - v->cyc + (v->perm[v->n - 1] + 1) * v->len + (j - v->cyc), a);
        } else {
            o[0] = b[0];  // SCRATCH comes back round to where it started
            o[1] = b[1];
            return;
        }
        mix2(o, a, b, (float)(j - v->cyc) * s_slew);
    } else {
        hread(t - v->cyc, o);  // from now on: the cycle, over and over
    }
}

static void switch_to(const Voice *v) {
    s_prev = s_cur;
    s_cur = *v;
    s_xf = 0;
}

// A new order for the bar's 16ths. Even 16ths stay on even steps and odd
// on odd, so the groove keeps its feel, and the downbeat stays first. A
// 16th moves at most `late` steps later than it was (the ring has to still
// hold it when it plays); earlier is always fine.
static void shuffle_perm(Voice *v, int late) {
    const int n = v->n;
    for (int i = 0; i < n; i++) v->perm[i] = (uint8_t)i;
    for (int tries = 0; tries < 96; tries++) {
        const int a = 1 + (int)(rng() % (uint32_t)(n - 1)), b = 1 + (int)(rng() % (uint32_t)(n - 1));
        if (a == b || ((a ^ b) & 1)) continue;
        uint8_t s = v->perm[a];
        v->perm[a] = v->perm[b];
        v->perm[b] = s;
        if (a - v->perm[a] > late || b - v->perm[b] > late) {  // too late: undo
            s = v->perm[a];
            v->perm[a] = v->perm[b];
            v->perm[b] = s;
        }
    }
    bool same = true;
    for (int i = 0; i < n; i++) same &= v->perm[i] == i;
    if (same && n >= 4) {  // never the bar as it was
        v->perm[1] = 3;
        v->perm[3] = 1;
    }
}

static Voice make_voice(int fx) {
    Voice v;
    memset(&v, 0, sizeof(v));
    v.fx = (int8_t)fx;
    v.e = s_t;
    const float beat = beat_frames();
    switch (fx) {
        case PUNCH_LOOP4: v.kind = V_LOOP, v.len = (uint32_t)lrintf(beat); break;
        case PUNCH_LOOP8: v.kind = V_LOOP, v.len = (uint32_t)lrintf(beat / 2.0f); break;
        case PUNCH_LOOP16: v.kind = V_LOOP, v.len = (uint32_t)lrintf(beat / 4.0f); break;
        case PUNCH_LOOP32: v.kind = V_LOOP, v.len = (uint32_t)lrintf(beat / 8.0f); break;
        case PUNCH_TRIPLET: v.kind = V_LOOP, v.len = (uint32_t)lrintf(beat / 3.0f); break;
        case PUNCH_REVERSE:  // (a beat, as long as the ring holds two)
            v.kind = V_REV, v.len = v.cyc = (uint32_t)fminf(beat, (float)(H_FRAMES / 2 - 2 * s_fade));
            break;
        case PUNCH_SCRATCH:
            v.kind = V_SCR, v.len = v.cyc = (uint32_t)lrintf(beat);
            v.depth = v.len / 4;
            break;
        case PUNCH_SHUFFLE: {
            // A whole bar, unless it's too slow for the ring to hold it
            // while the new order plays (a 16th played n + late steps after
            // it was recorded must still be there).
            v.kind = V_SHUF;
            v.len = (uint32_t)lrintf(beat / 4.0f);
            int n = 16, late;
            for (;;) {
                late = (int)((H_FRAMES - 2 * s_fade) / v.len) - n - 1;
                if (late >= 3 || n <= 4) break;
                n -= 2;
            }
            if (late < 2) late = 2;
            v.n = (uint8_t)n;
            v.cyc = (uint32_t)n * v.len;
            shuffle_perm(&v, late);
            break;
        }
        default: v.kind = V_DRY, v.fx = -1; break;
    }
    return v;
}

// Which repeater the held / sequenced pads ask for (shortest loop first).
static int repeater(uint16_t m) {
    static const int8_t order[] = {PUNCH_LOOP32, PUNCH_LOOP16, PUNCH_TRIPLET, PUNCH_LOOP8, PUNCH_LOOP4,
                                   PUNCH_SCRATCH, PUNCH_REVERSE, PUNCH_SHUFFLE};
    for (unsigned i = 0; i < sizeof(order); i++)
        if (m & PUNCH_BIT(order[i])) return order[i];
    return -1;
}

// A 1/16 tick: what's wanted now is what's on.
static void tick(uint16_t want) {
    const uint16_t was = s_active;
    s_active = want;
    const int fx = repeater(want);
    const int target = s_pend.kind != V_DRY ? s_pend.fx : s_cur.fx;
    if (fx == s_cur.fx && s_pend.kind != V_DRY) {
        s_pend.kind = V_DRY;  // back to the one still playing: nothing to capture
    } else if (fx != target) {
        Voice v = make_voice(fx);
        if (v.kind == V_LOOP) {
            s_pend = v;  // the old voice plays (and is recorded) through the first cycle
        } else {
            s_pend.kind = V_DRY;
            switch_to(&v);
        }
    }
    if ((want & PUNCH_BIT(PUNCH_RISER)) && !(was & PUNCH_BIT(PUNCH_RISER))) s_riser_e = s_t;
    if ((want & PUNCH_BIT(PUNCH_DUB)) && !s_dub_on) {
        float d = beat_frames() * 0.75f;  // a dotted 1/8 (halved until it fits)
        while (d > E_FRAMES - 256) d *= 0.5f;
        s_dub_d = (uint32_t)lrintf(d);
        s_dub_on = true;
        s_dub_win = 0;
        s_dub_peak = 0.0f;
        s_dfb[0] = s_dfb[1] = s_dhp[0] = s_dhp[1] = s_dhpx[0] = s_dhpx[1] = 0.0f;
    }
}

// ------------------------------------------------------------- API ----

void punch_init(int sample_rate) {
    s_rate = (float)sample_rate;
    s_fade = (uint32_t)(0.006f * s_rate);
    s_slew = 1.0f / (float)s_fade;
    s_win = 0.06f * s_rate;
    memset(s_h, 0, sizeof(s_h));
    memset(s_l, 0, sizeof(s_l));
    memset(s_e, 0, sizeof(s_e));
    s_t = 0;
    s_live = 0;
    __atomic_store_n(&s_tap, 0, __ATOMIC_RELAXED);
    s_pat = s_active = 0;
    s_since = 0.0;
    s_step = -1;
    s_step16 = 0;
    memset(&s_cur, 0, sizeof(s_cur));
    s_cur.fx = -1;
    s_prev = s_pend = s_cur;
    s_xf = s_fade;
    s_pmix = s_lmix = s_kl_mix = s_kh_mix = s_rf_mix = s_gmix = s_dsend = 0.0f;
    s_g = 1.0f;
    s_dub_on = false;
}

void punch_set_live(uint16_t mask) {
    const uint16_t added = (uint16_t)(mask & ~s_live);
    s_live = mask;
    if (added) __atomic_fetch_or(&s_tap, (uint32_t)added, __ATOMIC_RELAXED);
}

void punch_clock(bool tick_now, int step, bool playing, uint16_t pattern, float bpm) {
    s_bpm = bpm < 20.0f ? 20.0f : bpm;
    const uint16_t live = s_live;
    bool t = false;
    if (playing) {
        if (tick_now) {
            t = true;
            s_pat = pattern;
            s_step = step;
            s_step16 = step & 15;
            s_since = 0.0;
        }
    } else {
        s_pat = 0;
        s_step = -1;
        const uint32_t taps = __atomic_load_n(&s_tap, __ATOMIC_RELAXED);
        const double len = sixteenth();
        if (s_active == 0 && (live | taps)) {  // no grid while stopped: it starts with you
            t = true;
            s_since = 0.0;
            s_step16 = 0;
        } else if (s_since >= len) {
            t = true;
            s_since -= len;
            if (s_since >= len) s_since = 0.0;
            s_step16 = (s_step16 + 1) & 15;
        }
    }
    s_playing = playing;
    if (t) tick((uint16_t)(live | __atomic_exchange_n(&s_tap, 0u, __ATOMIC_RELAXED) | s_pat));
}

PunchState punch_state(void) {
    PunchState st;
    st.active = s_pub.active;
    st.want = (uint16_t)(s_live | s_pat | __atomic_load_n(&s_tap, __ATOMIC_RELAXED));
    st.step = s_pub.step;
    st.step16 = s_pub.step16;
    st.gate = s_pub.gate;
    st.riser = s_pub.riser;
    st.scratch = s_pub.scratch;
    st.echo = s_pub.echo;
    st.slice = s_pub.slice;
    st.repeats = s_pub.repeats;
    return st;
}

static bool idle(void) {
    return s_active == 0 && s_cur.kind == V_DRY && s_pend.kind == V_DRY && s_xf >= s_fade && s_pmix <= 0.0f &&
           s_lmix <= 0.0f && s_kl_mix <= 0.0f && s_kh_mix <= 0.0f && s_rf_mix <= 0.0f && s_gmix <= 0.0f && !s_dub_on;
}

static void publish(void) {
    s_pub.active = s_active;
    s_pub.step = s_step;
    s_pub.step16 = s_step16;
    s_pub.gate = 1.0f - s_gmix * (1.0f - s_g);
    s_pub.riser = (s_active & PUNCH_BIT(PUNCH_RISER)) ? s_riser : 0.0f;
    s_pub.echo = s_dub_on ? fminf(1.0f, s_dub_lvl * 3.0f) : 0.0f;
    float sc = 0.0f;
    int slice = -1;
    uint32_t reps = 0;
    const uint32_t j = s_t - s_cur.e;
    if (s_cur.kind == V_SCR) sc = fcos(2.0f * (float)(j % s_cur.len) / (float)s_cur.len);
    if (s_cur.kind == V_SHUF) slice = s_cur.perm[(j % s_cur.cyc) / s_cur.len];
    if (s_cur.kind == V_LOOP) reps = j / s_cur.len + 1;
    s_pub.scratch = sc;
    s_pub.slice = slice;
    s_pub.repeats = reps;
}

void punch_process(int16_t *io, int frames) {
    if (idle()) {  // nothing on: the audio passes untouched, the rings keep up
        for (int n = 0; n < frames; n++, s_t++) {
            s_h[s_t & H_MASK][0] = s_l[s_t & L_MASK][0] = io[2 * n];
            s_h[s_t & H_MASK][1] = s_l[s_t & L_MASK][1] = io[2 * n + 1];
        }
        s_since += frames;
        s_riser = 0.0f;
        publish();
        return;
    }
    const uint16_t on = s_active;
    const bool want_pitch = on & (PUNCH_BIT(PUNCH_OCT_UP) | PUNCH_BIT(PUNCH_OCT_DOWN) | PUNCH_BIT(PUNCH_RISER));
    const bool want_lofi = on & PUNCH_BIT(PUNCH_LOFI), want_kl = on & PUNCH_BIT(PUNCH_KILL_LOW);
    const bool want_kh = on & PUNCH_BIT(PUNCH_KILL_HIGH), want_rise = on & PUNCH_BIT(PUNCH_RISER);
    const bool want_gate = on & PUNCH_BIT(PUNCH_GATE), want_dub = on & PUNCH_BIT(PUNCH_DUB);

    // Per block: the pitch ratio, the riser's climb, the filters' corners.
    if (want_rise) s_riser = fminf(1.0f, (float)(s_t - s_riser_e) / (beat_frames() * 4.0f));
    if (want_pitch) {
        float r = (on & PUNCH_BIT(PUNCH_OCT_UP)) ? 2.0f : (on & PUNCH_BIT(PUNCH_OCT_DOWN)) ? 0.5f : 1.0f;
        if (want_rise) r *= exp2f(s_riser);
        s_ratio = r;
    }
    s_kl_f = svf_f(280.0f);
    s_kh_f = svf_f(380.0f);
    if (want_rise) s_rf_f = svf_f(30.0f * powf(60.0f, s_riser));  // 30 Hz -> 1.8 kHz
    const float step_len = sixteenth();
    const float lofi_lp = 1.0f - expf(-6.2831853f * 2800.0f / s_rate);
    const float lofi_hp = expf(-6.2831853f * 180.0f / s_rate);
    const float dub_hp = expf(-6.2831853f * 220.0f / s_rate), dub_lp = 1.0f - expf(-6.2831853f * 2600.0f / s_rate);
    const float gate_k = 1.0f / (0.002f * s_rate);
    // Per-sample steps, divided once here (divides are slow on the ARM11).
    const float phi_step = (1.0f - s_ratio) / s_win, wow_step = 0.55f / s_rate, flut_step = 6.5f / s_rate;
    const uint32_t pop_odds = (uint32_t)(65536.0f * 7.0f / s_rate);

    for (int n = 0; n < frames; n++, s_t++) {
        const float in[2] = {q15(io[2 * n]), q15(io[2 * n + 1])};
        const uint32_t t = s_t;

        // --- the repeater ---
        if (s_pend.kind != V_DRY && t - s_pend.e >= s_pend.len) {  // a LOOP's first cycle is in
            switch_to(&s_pend);
            s_pend.kind = V_DRY;
        }
        float a[2];
        if (s_xf < s_fade) {
            float p[2], c[2];
            pure(&s_prev, t, in, p);
            pure(&s_cur, t, in, c);
            mix2(a, p, c, (float)s_xf * s_slew);
            s_xf++;
        } else {
            pure(&s_cur, t, in, a);
        }
        s_h[t & H_MASK][0] = to16(a[0]);
        s_h[t & H_MASK][1] = to16(a[1]);
        a[0] = q15(s_h[t & H_MASK][0]);  // exactly what H holds: repeats stay bit-exact
        a[1] = q15(s_h[t & H_MASK][1]);

        // --- pitch: two taps sliding through H, crossfading ---
        if (want_pitch || s_pmix > 0.0f) {
            if (s_pmix <= 0.0f) s_phi = 0.5f;  // start on one tap, a little behind
            s_pmix = slew_to(s_pmix, want_pitch);
            float pa[2], pb[2];
            float phb = s_phi + 0.5f;
            if (phb >= 1.0f) phb -= 1.0f;
            hback(t, s_phi * s_win, pa);
            hback(t, phb * s_win, pb);
            const float ga = 1.0f - fabsf(2.0f * s_phi - 1.0f);
            const float w[2] = {pb[0] + (pa[0] - pb[0]) * ga, pb[1] + (pa[1] - pb[1]) * ga};
            mix2(a, a, w, s_pmix);
            s_phi += phi_step;
            if (s_phi < 0.0f) s_phi += 1.0f;
            if (s_phi >= 1.0f) s_phi -= 1.0f;
        }

        // --- LO-FI ---
        s_l[t & L_MASK][0] = to16(a[0]);
        s_l[t & L_MASK][1] = to16(a[1]);
        if (want_lofi || s_lmix > 0.0f) {
            if (s_lmix <= 0.0f) {
                s_ltone[0] = a[0], s_ltone[1] = a[1];
                s_lhp[0] = s_lhp[1] = 0.0f;
                s_lhpx[0] = a[0], s_lhpx[1] = a[1];
            }
            s_lmix = slew_to(s_lmix, want_lofi);
            s_wow += wow_step;
            s_flut += flut_step;
            if (s_wow >= 1.0f) s_wow -= 1.0f;
            if (s_flut >= 1.0f) s_flut -= 1.0f;
            const float d = s_rate * (0.008f + 0.0035f * fsin(s_wow) + 0.00025f * fsin(s_flut));
            const uint32_t di = (uint32_t)d;
            const float f = d - (float)di;
            const int16_t *x0 = s_l[(t - di) & L_MASK], *x1 = s_l[(t - di - 1) & L_MASK];
            // Dust: now and then a pop (a few a second), and a little hiss.
            if ((rng() & 0xFFFF) < pop_odds) s_pop = ((rng() & 1) ? 1.0f : -1.0f) * (0.02f + (rng() & 0xFF) / 255.0f * 0.03f);
            const float dust = s_pop + ((float)(rng() & 0xFFFF) / 65536.0f - 0.5f) * 0.004f;
            s_pop *= 0.55f;
            for (int c = 0; c < 2; c++) {
                const float v = q15(x0[c]) + (q15(x1[c]) - q15(x0[c])) * f;
                s_ltone[c] += (v + dust - s_ltone[c]) * lofi_lp;  // a small, dull speaker
                s_lhp[c] = lofi_hp * (s_lhp[c] + s_ltone[c] - s_lhpx[c]);
                s_lhpx[c] = s_ltone[c];
                const float wet = s_lhp[c] * 1.15f;
                a[c] += (wet - a[c]) * s_lmix;
            }
        }

        // --- the filters ---
        if (want_kl || s_kl_mix > 0.0f) {
            if (s_kl_mix <= 0.0f) memset(s_kl, 0, sizeof(s_kl));
            s_kl_mix = slew_to(s_kl_mix, want_kl);
            for (int c = 0; c < 2; c++) {
                const float w = svf(&s_kl[c][1], svf(&s_kl[c][0], a[c], s_kl_f, true), s_kl_f, true);
                a[c] += (w - a[c]) * s_kl_mix;
            }
        }
        if (want_kh || s_kh_mix > 0.0f) {
            if (s_kh_mix <= 0.0f) memset(s_kh, 0, sizeof(s_kh));
            s_kh_mix = slew_to(s_kh_mix, want_kh);
            for (int c = 0; c < 2; c++) {
                const float w = svf(&s_kh[c][1], svf(&s_kh[c][0], a[c], s_kh_f, false), s_kh_f, false);
                a[c] += (w - a[c]) * s_kh_mix;
            }
        }
        if (want_rise || s_rf_mix > 0.0f) {
            if (s_rf_mix <= 0.0f) memset(s_rf, 0, sizeof(s_rf));
            s_rf_mix = slew_to(s_rf_mix, want_rise);
            for (int c = 0; c < 2; c++) a[c] += (svf(&s_rf[c], a[c], s_rf_f, true) * 1.2f - a[c]) * s_rf_mix;
        }

        // --- GATE: open for 3/4 of each 16th the pattern has, closed otherwise ---
        if (want_gate || s_gmix > 0.0f) {
            if (s_gmix <= 0.0f) s_g = 1.0f;
            s_gmix = slew_to(s_gmix, want_gate);
            const bool open = ((PUNCH_GATE_PATTERN >> s_step16) & 1u) && s_since + n < step_len * 0.75f;
            s_g += ((open ? 1.0f : 0.0f) - s_g) * gate_k;
            const float g = 1.0f - s_gmix * (1.0f - s_g);
            a[0] *= g;
            a[1] *= g;
        }

        // --- DUB: a ping-pong echo; the send follows the pad, the echo rings on ---
        if (s_dub_on) {
            s_dsend = slew_to(s_dsend, want_dub);
            const int16_t *r = s_e[(t - s_dub_d) & E_MASK];
            const float wl = q15(r[0]), wr = q15(r[1]);
            float fb[2];
            for (int c = 0; c < 2; c++) {  // the repeats get darker and thinner
                const float x = c ? wr : wl;
                s_dfb[c] += (x - s_dfb[c]) * dub_lp;
                s_dhp[c] = dub_hp * (s_dhp[c] + s_dfb[c] - s_dhpx[c]);
                s_dhpx[c] = s_dfb[c];
                fb[c] = s_dhp[c];
            }
            const float send = s_dsend * 0.5f * (a[0] + a[1]);
            const float il = send + 0.62f * fb[1], ir = 0.62f * fb[0];
            s_e[t & E_MASK][0] = to16(il);
            s_e[t & E_MASK][1] = to16(ir);
            a[0] += wl * 0.8f;
            a[1] += wr * 0.8f;
            // (What goes in counts too: the ring isn't quiet until a whole
            // delay's worth of it has been written quiet.)
            const float pk = fmaxf(fmaxf(fabsf(wl), fabsf(wr)), fmaxf(fabsf(il), fabsf(ir)));
            if (pk > s_dub_peak) s_dub_peak = pk;
            if (++s_dub_win >= s_dub_d) {  // once a whole delay has gone by silent: done
                s_dub_lvl = s_dub_peak;
                if (s_dsend <= 0.0f && s_dub_peak < 2.5f / 32768.0f) s_dub_on = false;
                s_dub_peak = 0.0f;
                s_dub_win = 0;
            }
        }

        io[2 * n] = to16(a[0]);
        io[2 * n + 1] = to16(a[1]);
    }
    s_since += frames;
    if (s_cur.kind == V_DRY && s_xf >= s_fade) s_prev = s_cur;
    publish();
}
