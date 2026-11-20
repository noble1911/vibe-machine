// tape_probe -- records a known tone onto the tape and checks playback,
// reverse, mute, clear and the idle pass-through.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tape.h"

#define RATE 44100
#define BLOCK 128

static int fails;
static void check(int ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static long s_t;
// Run `secs`; feed a 440 Hz tone if `tone`, else silence. Returns output RMS
// and (via *corr) correlation with a 440 Hz sine at the tape's own clock.
static double run(double secs, int tone, double *pitch_hz) {
    int16_t b[BLOCK * 2];
    double sq = 0;
    long n = 0, crossings = 0;
    int prev = 0;
    for (int done = 0; done < (int)(secs * RATE); done += BLOCK) {
        for (int i = 0; i < BLOCK; i++, s_t++) {
            int16_t v = tone ? (int16_t)(0.5 * 32767 * sin(2 * M_PI * 440.0 * s_t / RATE)) : 0;
            b[2 * i] = b[2 * i + 1] = v;
        }
        tape_process(b, BLOCK, RATE);
        for (int i = 0; i < BLOCK; i++) {
            int x = b[2 * i];
            sq += (double)x * x;
            if ((x >= 0) != (prev >= 0)) crossings++;
            prev = x;
            n++;
        }
    }
    if (pitch_hz) *pitch_hz = crossings / 2.0 / secs;
    return sqrt(sq / n) / 32768.0;
}

int main(void) {
    check(tape_init(), "tape memory allocates");

    // Idle: bit-exact pass-through.
    int16_t in[BLOCK * 2], out[BLOCK * 2];
    for (int i = 0; i < BLOCK * 2; i++) in[i] = (int16_t)(i * 97 - 9000);
    memcpy(out, in, sizeof(in));
    tape_process(out, BLOCK, RATE);
    check(memcmp(in, out, sizeof(in)) == 0, "a stopped tape leaves the live sound untouched");

    // Record 2 s of tone on track 0.
    tape_select(0);
    tape_record(true);
    tape_play(true);
    run(2.0, 1, NULL);
    tape_play(false);
    tape_record(false);
    run(0.3, 0, NULL);
    TapeStatus st = tape_status();
    check(st.used[0] && !st.used[1], "recording marks only the armed track as used");

    // Play it back over silence.
    tape_rewind();
    tape_play(true);
    double hz;
    double rms = run(1.5, 0, &hz);
    printf("  playback: rms %.3f  pitch %.0f Hz\n", rms, hz);
    check(rms > 0.2 && fabs(hz - 440) < 15, "playback returns the tone at its pitch");

    // Double speed: an octave up.
    tape_rewind();
    tape_set_speed(2.0f);
    run(0.2, 0, NULL);
    run(0.5, 0, &hz);
    printf("  double speed: pitch %.0f Hz\n", hz);
    check(fabs(hz - 880) < 30, "double speed plays an octave up");

    // Reverse: still the same pitch, head moving backwards.
    tape_set_speed(-1.0f);
    run(0.2, 0, NULL);
    float before = tape_status().seconds;
    run(0.4, 0, &hz);
    float after = tape_status().seconds;
    printf("  reverse: pitch %.0f Hz, head %.2f -> %.2f s\n", hz, before, after);
    check(fabs(hz - 440) < 15 && after < before, "reverse plays backwards at pitch");

    // Mute and clear.
    tape_set_speed(1.0f);
    tape_rewind();
    tape_mute(0, true);
    rms = run(0.5, 0, NULL);
    check(rms < 0.001, "a muted track is silent");
    tape_mute(0, false);
    tape_clear(0);
    rms = run(0.5, 0, NULL);
    check(rms < 0.001 && !tape_status().used[0], "a cleared track is empty");

    tape_exit();
    printf(fails ? "\n%d tape problem(s)\n" : "\nall tape OK\n", fails);
    return fails ? 1 : 0;
}
