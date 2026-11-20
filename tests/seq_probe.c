// seq_probe -- drives the sequencer on the build machine, timestamping every
// note it emits, to check timing (straight and swung), accents, pattern
// slots switching at the bar, and song chaining.
#include <math.h>
#include <stdlib.h>
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

static Hit s_hits[4096], s_offs[4096];
static int s_nhits, s_noffs;
static long s_clock;  // frames rendered so far

static void record(int synth, int note, float vel) {
    if (vel > 0.0f && s_nhits < 4096) s_hits[s_nhits++] = (Hit){s_clock, synth, note, vel};
    if (vel <= 0.0f && s_noffs < 4096) s_offs[s_noffs++] = (Hit){s_clock, synth, note, vel};
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

    // --- piano roll ---------------------------------------------------------
    const double step = 5512.5;
    seq_set_song(false);
    seq_set_swing(0.0f);
    SeqPattern roll;
    memset(&roll, 0, sizeof(roll));
    roll.notes[roll.count++] = (SeqRollNote){0, 60, 4, 200, 0};   // a long C
    roll.notes[roll.count++] = (SeqRollNote){8, 64, 1, 200, 0};   // a chord: E G B
    roll.notes[roll.count++] = (SeqRollNote){8, 67, 1, 200, 0};
    roll.notes[roll.count++] = (SeqRollNote){8, 71, 1, 200, 0};
    roll.notes[roll.count++] = (SeqRollNote){15, 72, 3, 200, 0};  // runs over the bar line
    seq_put_slot(6, &roll);
    seq_select_slot(6);
    s_nhits = s_noffs = 0;
    s_clock = 0;
    seq_toggle_play();
    run(2.6);
    seq_toggle_play();
    run(0.1);
    long on60 = -1, off60 = -1, off72 = -1;
    int chord = 0;
    for (int i = 0; i < s_nhits; i++) {
        if (s_hits[i].note == 60 && on60 < 0) on60 = s_hits[i].t;
        if (s_hits[i].t >= (long)(8 * step) - BLOCK && s_hits[i].t <= (long)(8 * step) + BLOCK &&
            (s_hits[i].note == 64 || s_hits[i].note == 67 || s_hits[i].note == 71))
            chord++;
    }
    for (int i = 0; i < s_noffs; i++) {
        if (s_offs[i].note == 60 && off60 < 0) off60 = s_offs[i].t;
        if (s_offs[i].note == 72 && off72 < 0) off72 = s_offs[i].t;
    }
    printf("  roll: C on %ld off %ld (ideal %.0f); chord notes together %d; bar-crossing note off %ld (ideal %.0f)\n",
           on60, off60, 3.55 * step, chord, off72, (15 + 2.55) * step);
    check(on60 == 0 && labs(off60 - (long)(3.55 * step)) <= BLOCK, "a 4-step note holds for its length");
    check(chord == 3, "three notes on one step sound together");
    check(labs(off72 - (long)(17.55 * step)) <= BLOCK, "a note can hold over the bar line");

    // Retrigger: a long C, then another C on step 2. The first must be let
    // go before the second starts, and its old release must not cut the
    // second short.
    memset(&roll, 0, sizeof(roll));
    roll.notes[roll.count++] = (SeqRollNote){0, 60, 6, 200, 0};
    roll.notes[roll.count++] = (SeqRollNote){2, 60, 1, 200, 0};
    seq_put_slot(6, &roll);
    s_nhits = s_noffs = 0;
    s_clock = 0;
    seq_toggle_play();
    run(0.9);  // about 7 steps
    seq_toggle_play();
    run(0.1);
    int offs = 0;
    long first_off = -1;
    for (int i = 0; i < s_noffs; i++)
        if (s_offs[i].note == 60) {
            offs++;
            if (first_off < 0) first_off = s_offs[i].t;
        }
    printf("  retrigger: %d note-offs for C, the first at %ld (ideal %.0f)\n", offs, first_off, 2 * step);
    check(offs == 2 && labs(first_off - (long)(2 * step)) <= BLOCK, "retriggering a held note releases it cleanly");

    // Live recording: hold a note for three steps and the roll keeps it that long.
    seq_select_slot(5);
    seq_clear();
    seq_set_recording(true);
    s_clock = 0;
    seq_toggle_play();
    run(step * 2.1 / RATE);  // just after step 2 starts
    SeqNote held = {2, 1, {64}};
    seq_record_synth(0, &held);
    run(step * 3.0 / RATE);
    seq_record_release(0, &held);
    seq_toggle_play();
    seq_set_recording(false);
    SeqPattern got;
    seq_snapshot(&got);
    printf("  recorded: %d note(s), step %d, length %d\n", got.count, got.count ? got.notes[0].step : -1,
           got.count ? got.notes[0].len : -1);
    check(got.count == 1 && got.notes[0].step == 2 && got.notes[0].len == 3, "a live note is recorded as long as it was held");

    // --- parts ----------------------------------------------------------------
    memset(&roll, 0, sizeof(roll));
    roll.notes[roll.count++] = (SeqRollNote){0, 48, 1, 200, 0};  // bass on part 1
    roll.notes[roll.count++] = (SeqRollNote){0, 72, 1, 200, 1};  // lead on part 2
    roll.notes[roll.count++] = (SeqRollNote){0, 72, 1, 200, 2};  // same pitch on part 3
    seq_put_slot(4, &roll);
    seq_select_slot(4);
    s_nhits = 0;
    seq_toggle_play();
    run(0.05);
    seq_toggle_play();
    run(0.05);
    int on_synth[4] = {0};
    for (int i = 0; i < s_nhits; i++)
        if (s_hits[i].synth >= 1 && s_hits[i].synth <= 3) on_synth[s_hits[i].synth]++;
    check(on_synth[1] == 1 && on_synth[2] == 1 && on_synth[3] == 1, "each part's notes go to its own synth, same pitch too");
    seq_set_part_mute(1, true);
    s_nhits = 0;
    seq_toggle_play();
    run(0.05);
    seq_toggle_play();
    run(0.05);
    int muted_hits = 0, others = 0;
    for (int i = 0; i < s_nhits; i++) {
        if (s_hits[i].synth == 2) muted_hits++;
        if (s_hits[i].synth == 1 || s_hits[i].synth == 3) others++;
    }
    seq_set_part_mute(1, false);
    check(muted_hits == 0 && others == 2, "a muted part is silent while the others play");

    printf(fails ? "\n%d sequencer problem(s)\n" : "\nall sequencer OK\n", fails);
    return fails ? 1 : 0;
}
