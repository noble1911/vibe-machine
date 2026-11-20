// fx_probe -- exercises fx.c on the build machine with a known test tone.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"

#define RATE 44100
#define BLOCK 128

static long s_t;  // test-signal clock, in frames

// A low and a high sine plus a slow ramp, so every repeat window differs.
static void test_tone(int16_t *buf, int frames) {
    for (int i = 0; i < frames; i++, s_t++) {
        double t = (double)s_t / RATE;
        double v = 0.3 * sin(2 * M_PI * 220 * t) + 0.2 * sin(2 * M_PI * 3520 * t) + 0.1 * sin(2 * M_PI * 0.7 * t);
        buf[2 * i] = buf[2 * i + 1] = (int16_t)(v * 32767);
    }
}

typedef struct {
    double rms, bright;
    int distinct;
} M;

static int16_t s_out[RATE * 4 * 2];

// Run `secs` of audio through the effects; keep the output in s_out.
static M run(double secs) {
    int frames = (int)(secs * RATE);
    double sq = 0, ab = 0, sl = 0;
    int prev = 0;
    static unsigned char seen[65536];
    memset(seen, 0, sizeof(seen));
    int distinct = 0;
    for (int done = 0; done < frames; done += BLOCK) {
        int16_t *b = s_out + 2 * done;
        test_tone(b, BLOCK);
        fx_process(b, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            int x = b[2 * i];
            if (isnan((double)x)) abort();
            sq += (double)x * x;
            ab += abs(x);
            sl += abs(x - prev);
            prev = x;
            if (!seen[x + 32768]) {
                seen[x + 32768] = 1;
                distinct++;
            }
        }
    }
    M m = {sqrt(sq / frames) / 32768.0, ab > 0 ? sl / ab : 0, distinct};
    return m;
}

static int fails;
static void check(int ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void) {
    fx_init(RATE);
    fx_set_tempo(120);

    M dry = run(0.5);
    // Bypass must be bit-exact (fx off from the start).
    int16_t ref[BLOCK * 2];
    long t0 = s_t;
    test_tone(ref, BLOCK);
    s_t = t0;
    M again = run((double)BLOCK / RATE);
    check(memcmp(ref, s_out, sizeof(ref)) == 0 && again.rms > 0, "bypass passes audio through unchanged");

    fx_set(FX_SWEEP, true, 0.05f, 0.2f);
    run(0.1);
    M lp = run(0.5);
    fx_set(FX_SWEEP, true, 0.95f, 0.2f);
    run(0.1);
    M hp = run(0.5);
    fx_set(FX_SWEEP, false, 0.5f, 0.2f);
    run(0.1);
    printf("  brightness dry %.3f  low-pass %.3f  high-pass %.3f\n", dry.bright, lp.bright, hp.bright);
    check(lp.bright < dry.bright * 0.6, "SWEEP low-pass darkens the sound");
    check(hp.bright > dry.bright * 1.4, "SWEEP high-pass brightens the sound");

    fx_set(FX_CRUSH, true, 1.0f, 1.0f);
    run(0.1);
    M cr = run(0.5);
    fx_set(FX_CRUSH, false, 1.0f, 1.0f);
    run(0.1);
    printf("  distinct sample values dry %d  crushed %d\n", dry.distinct, cr.distinct);
    check(cr.distinct < 16, "CRUSH reduces the signal to a handful of levels");

    // STUTTER at 1/32 note, 120 BPM: a repeat every 0.0625 s = 2756 frames.
    fx_set(FX_STUTTER, true, 1.0f, 0.0f);
    run(0.2);
    run(0.5);
    const int L = (int)(RATE * 60.0 / 120 * 0.125);
    double diff = 0;
    for (int i = 4000; i < 8000; i++) diff += abs(s_out[2 * i] - s_out[2 * (i + L)]);
    diff /= 4000 * 32768.0;
    fx_set(FX_STUTTER, false, 1.0f, 0.0f);
    run(0.2);
    printf("  stutter: mean |x[n] - x[n+%d]| = %.5f\n", L, diff);
    check(diff < 0.002, "STUTTER repeats exactly at the tempo-synced length");

    fx_set(FX_TAPE, true, 0.0f, 0.0f);  // fastest stop: 0.15 s
    run(0.4);
    M stopped = run(0.3);
    FxState st = fx_state();
    check(stopped.rms < 0.01 && st.tape_speed < 0.01, "TAPE grinds to a halt");
    fx_set(FX_TAPE, false, 0.0f, 0.0f);
    run(0.5);
    M back = run(0.5);
    printf("  tape: rms stopped %.4f  recovered %.4f (dry %.4f)\n", stopped.rms, back.rms, dry.rms);
    check(fabs(back.rms - dry.rms) < dry.rms * 0.1 && fx_state().tape_speed >= 1.0f, "TAPE spins back up to live");

    printf(fails ? "\n%d FX problem(s)\n" : "\nall FX OK\n", fails);
    return fails ? 1 : 0;
}
