// seq_probe -- drives the sequencer on the build machine, timestamping every
// note it emits, to check timing (straight and swung), accents, pattern
// slots switching at the bar, and song chaining.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine.h"
#include "seq.h"

#define RATE 44100
#define BLOCK 128

typedef struct {
    long t;
    int synth, note;
    float vel;
} Hit;

static Hit s_hits[4096];
static int s_nhits;
static long s_clock;  // frames rendered so far

static void record(int synth, int note, float vel) {
    if (vel > 0.0f && s_nhits < 4096) s_hits[s_nhits++] = (Hit){s_clock, synth, note, vel};
}

static void run(double secs) {
    for (long f = 0; f < (long)(secs * RATE); f += BLOCK) {
        seq_advance(BLOCK, RATE);
        s_clock += BLOCK;
    }
}

static int fails;
static void check(int ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static SeqPattern only_lane(int lane) {  // one drum lane on every step
    SeqPattern p;
    memset(&p, 0, sizeof(p));
    for (int s = 0; s < SEQ_STEPS; s++) p.drums[s] = (uint8_t)(1u << lane);
    return p;
}

int main(void) {
    seq_init(record);
    seq_set_bpm(120);  // a 16th = 5512.5 frames
    seq_put_slot(0, &(SeqPattern){0});
    SeqPattern hats = only_lane(2);
    hats.accents = 1u << 0;
    seq_put_slot(0, &hats);

    seq_toggle_play();
    run(2.0);
    seq_toggle_play();
    double sum = 0;
    int n = 0;
    for (int i = 1; i < s_nhits; i++, n++) sum += s_hits[i].t - s_hits[i - 1].t;
    printf("  straight: %d hits, mean gap %.1f frames (ideal 5512.5)\n", s_nhits, sum / n);
    check(fabs(sum / n - 5512.5) < 64, "straight 16ths are evenly spaced at the tempo");
    check(s_hits[0].vel == SEQ_VEL_DRUM_ACCENT && s_hits[1].vel == SEQ_VEL_DRUM, "accented steps hit harder than plain ones");

    // Swing 50%: gaps alternate 1.5x and 0.5x of a straight 16th.
    s_nhits = 0;
    seq_set_swing(0.5f);
    seq_toggle_play();
    run(2.0);
    seq_toggle_play();
    double even = 0, odd = 0;
    int ne = 0, no = 0;
    for (int i = 1; i < s_nhits; i++) {
        double gap = s_hits[i].t - s_hits[i - 1].t;
        if ((i - 1) % 2 == 0) even += gap, ne++;
        else odd += gap, no++;
    }
    printf("  swing 50%%: gaps after even steps %.0f, after odd steps %.0f (ideal 8269 / 2756)\n", even / ne, odd / no);
    check(fabs(even / ne - 8268.75) < 130 && fabs(odd / no - 2756.25) < 130, "swing lengthens even steps and shortens odd ones");
    seq_set_swing(0.0f);

    // Slot B (crash only) queued mid-bar: nothing from B until the next bar.
    seq_put_slot(1, &(SeqPattern){0});
    SeqPattern crash = only_lane(7);
    seq_put_slot(1, &crash);
    s_nhits = 0;
    s_clock = 0;
    seq_select_slot(0);
    seq_toggle_play();
    run(0.5);  // about step 4 of A
    seq_select_slot(1);
    long queued_at = s_clock;
    run(3.0);
    seq_toggle_play();
    const long bar = (long)(5512.5 * 16);
    long first_crash = -1;
    for (int i = 0; i < s_nhits; i++)
        if (s_hits[i].note == 49) {
            first_crash = s_hits[i].t;
            break;
        }
    printf("  queued at frame %ld, first crash at %ld (next bar at %ld)\n", queued_at, first_crash, bar);
    check(first_crash >= bar - BLOCK && first_crash <= bar + BLOCK, "a queued slot takes over exactly at the next bar");

    // Song: A (hats), B (crash), C empty, D (kick) -> A, B, D, A ...
    SeqPattern kicks = only_lane(0);
    seq_put_slot(2, &(SeqPattern){0});
    seq_put_slot(3, &kicks);
    seq_select_slot(0);
    seq_set_song(true);
    s_nhits = 0;
    s_clock = 0;
    seq_toggle_play();
    run(4 * 2.0 + 0.1);  // four bars at 120 BPM
    seq_toggle_play();
    seq_set_song(false);
    int order[4];
    for (int b = 0; b < 4; b++) {
        long t = b * bar + 10;
        int note = -1;
        for (int i = 0; i < s_nhits; i++)
            if (s_hits[i].t >= b * bar - BLOCK && s_hits[i].t <= t + BLOCK) {
                note = s_hits[i].note;
                break;
            }
        order[b] = note;
    }
    printf("  song bars play notes %d %d %d %d (hat 42, crash 49, kick 36)\n", order[0], order[1], order[2], order[3]);
    check(order[0] == 42 && order[1] == 49 && order[2] == 36 && order[3] == 42, "song mode chains the used slots, skipping empty ones");

    // Copy A into H and make sure H now counts as used.
    seq_select_slot(0);
    seq_copy_to_slot(7);
    SeqStatus st = seq_status();
    check(st.slot == 7 && (st.used & 0x80), "copying a pattern into a slot selects and fills it");

    printf(fails ? "\n%d sequencer problem(s)\n" : "\nall sequencer OK\n", fails);
    return fails ? 1 : 0;
}
