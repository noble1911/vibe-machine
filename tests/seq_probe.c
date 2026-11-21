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
    roll.notes[roll.count++] = (SeqRollNote){0, 60, 4, 200, 0, 0};   // a long C
    roll.notes[roll.count++] = (SeqRollNote){8, 64, 1, 200, 0, 0};   // a chord: E G B
    roll.notes[roll.count++] = (SeqRollNote){8, 67, 1, 200, 0, 0};
    roll.notes[roll.count++] = (SeqRollNote){8, 71, 1, 200, 0, 0};
    roll.notes[roll.count++] = (SeqRollNote){15, 72, 3, 200, 0, 0};  // runs over the bar line
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
    roll.notes[roll.count++] = (SeqRollNote){0, 60, 6, 200, 0, 0};
    roll.notes[roll.count++] = (SeqRollNote){2, 60, 1, 200, 0, 0};
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
    roll.notes[roll.count++] = (SeqRollNote){0, 48, 1, 200, 0, 0};  // bass on part 1
    roll.notes[roll.count++] = (SeqRollNote){0, 72, 1, 200, 1, 0};  // lead on part 2
    roll.notes[roll.count++] = (SeqRollNote){0, 72, 1, 200, 2, 0};  // same pitch on part 3
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

    roll.drums[0] = 0x3;  // kick and snare on step 1
    seq_put_slot(4, &roll);
    seq_set_part_mute(PARTS, true);  // the drums
    s_nhits = 0;
    seq_toggle_play();
    run(0.05);
    seq_toggle_play();
    run(0.05);
    int drum_hits = 0, part_hits = 0;
    for (int i = 0; i < s_nhits; i++) {
        if (s_hits[i].synth == SYNTH_DRUMS && s_hits[i].vel > 0) drum_hits++;
        if (s_hits[i].synth >= 1 && s_hits[i].synth <= 3 && s_hits[i].vel > 0) part_hits++;
    }
    seq_set_part_mute(PARTS, false);
    check(drum_hits == 0 && part_hits == 3, "muted drums are silent while the parts play");

    // --- arrangement ----------------------------------------------------------
    // A x2, then B once with part 1 dropped out, then stop.
    {
        SeqPattern pa, pb;
        memset(&pa, 0, sizeof(pa));
        memset(&pb, 0, sizeof(pb));
        pa.drums[0] = 1u << 0;  // kick
        pb.drums[0] = 1u << 1;  // snare
        pb.notes[pb.count++] = (SeqRollNote){0, 48, 1, 200, 0, 0};  // part 1: dropped by the row
        pb.notes[pb.count++] = (SeqRollNote){0, 72, 1, 200, 1, 0};  // part 2: plays
        seq_put_slot(0, &pa);
        seq_put_slot(1, &pb);
        for (int i = 2; i < SEQ_SLOTS; i++) {
            SeqPattern empty;
            memset(&empty, 0, sizeof(empty));
            seq_put_slot(i, &empty);
        }
        SeqArrangement arr;
        memset(&arr, 0, sizeof(arr));
        arr.rows[arr.count++] = (SeqArrRow){0, 2, 0};
        arr.rows[arr.count++] = (SeqArrRow){1, 1, 1u << 0};
        arr.loop = false;
        seq_set_arrangement(&arr);
        seq_set_song(true);
        seq_set_bpm(120);  // a bar is 2 s
        seq_select_slot(1);  // wherever we were, the song starts at its first row
        const uint32_t ends0 = seq_status().ends;
        s_nhits = 0;
        seq_toggle_play();
        run(7.0);
        SeqStatus st = seq_status();
        int drums[8], nd = 0, part1 = 0, part2 = 0;
        for (int i = 0; i < s_nhits; i++) {
            if (s_hits[i].vel <= 0) continue;
            if (s_hits[i].synth == SYNTH_DRUMS && nd < 8) drums[nd++] = s_hits[i].note;
            if (s_hits[i].synth == part_synth(0)) part1++;
            if (s_hits[i].synth == part_synth(1)) part2++;
        }
        printf("  arrangement: %d drum hits, part 1 %d, part 2 %d, playing %d\n", nd, part1, part2, st.playing);
        check(nd == 3 && drums[0] == drums[1] && drums[2] != drums[0], "rows play their slots, repeated: A A B");
        check(part1 == 0 && part2 == 1, "a row drops out the parts it mutes");
        check(!st.playing && st.ends == ends0 + 1, "the song stops at the end of the arrangement");
        arr.loop = true;
        seq_set_arrangement(&arr);
        s_nhits = 0;
        seq_toggle_play();
        run(7.0);
        nd = 0;
        for (int i = 0; i < s_nhits; i++)
            if (s_hits[i].vel > 0 && s_hits[i].synth == SYNTH_DRUMS) nd++;
        check(seq_status().playing && nd == 4, "LOOP goes round again instead");
        seq_toggle_play();
        run(0.1);
        seq_set_song(false);
    }

    // --- patterns longer than a bar ---------------------------------------------
    {
        SeqPattern two;
        memset(&two, 0, sizeof(two));
        two.len = 32;
        two.drums[0] = 1u << 0;   // kick on bar 1
        two.drums[16] = 1u << 1;  // snare on bar 2
        seq_put_slot(0, &two);
        seq_set_song(false);
        seq_select_slot(0);
        seq_set_bpm(120);  // a bar is 2 s
        s_nhits = 0;
        seq_toggle_play();
        run(5.0);
        seq_toggle_play();
        run(0.1);
        long t[8];
        int note[8], nd = 0;
        for (int i = 0; i < s_nhits && nd < 8; i++)
            if (s_hits[i].synth == SYNTH_DRUMS && s_hits[i].vel > 0) {
                t[nd] = s_hits[i].t;
                note[nd++] = s_hits[i].note;
            }
        printf("  2-bar pattern: %d hits at %.2f, %.2f, %.2f s\n", nd, nd > 0 ? t[0] / 44100.0 : -1, nd > 1 ? t[1] / 44100.0 : -1,
               nd > 2 ? t[2] / 44100.0 : -1);
        check(nd == 3 && note[0] == note[2] && note[1] != note[0] && labs(t[1] - t[0] - 88200) < 200 &&
                  labs(t[2] - t[0] - 176400) < 200,
              "a 2-bar pattern plays bar 2 before going round");

        // Growing it to 4 bars copies bars 1-2 into 3-4.
        SeqPattern one;
        memset(&one, 0, sizeof(one));
        one.drums[4] = 1u << 2;
        one.notes[one.count++] = (SeqRollNote){8, 60, 2, 200, 0, 0};
        seq_put_slot(1, &one);
        seq_select_slot(1);
        seq_set_bars(4);
        SeqPattern got;
        seq_get_slot(1, &got);
        int copies = 0;
        for (int i = 0; i < got.count; i++) copies += got.notes[i].pitch == 60 && got.notes[i].step % 16 == 8;
        check(seq_len(&got) == 64 && got.drums[20] == got.drums[4] && got.drums[52] == got.drums[4] && copies == 4,
              "growing a pattern to 4 bars repeats it into the new bars");
        seq_set_bars(1);
        seq_get_slot(1, &got);
        check(seq_len(&got) == 16 && got.drums[4] == (1u << 2), "shrinking keeps the first bar");

        // A sliding note stays a slide when the pattern grows, and when a
        // part's notes are replaced (the dice, clearing in the roll).
        one.notes[0].flags = SEQ_NOTE_SLIDE;
        seq_put_slot(1, &one);
        seq_set_bars(2);
        seq_get_slot(1, &got);
        int slides = 0;
        for (int i = 0; i < got.count; i++) slides += (got.notes[i].flags & SEQ_NOTE_SLIDE) != 0;
        SeqPattern part2;
        memset(&part2, 0, sizeof(part2));
        part2.notes[part2.count++] = (SeqRollNote){3, 62, 1, 200, 0, SEQ_NOTE_SLIDE};
        seq_set_part_pattern(1, &part2);
        seq_get_slot(1, &got);
        int kept = 0;
        for (int i = 0; i < got.count; i++) kept += (got.notes[i].flags & SEQ_NOTE_SLIDE) != 0;
        check(slides == 2 && kept == 3, "slides survive growing a pattern and replacing a part's notes");
        seq_set_bars(1);

        // An arrangement row of a 2-bar pattern x2 is 4 bars long.
        SeqArrangement arr;
        memset(&arr, 0, sizeof(arr));
        arr.rows[arr.count++] = (SeqArrRow){0, 2, 0};
        arr.loop = false;
        seq_set_arrangement(&arr);
        seq_set_song(true);
        const uint32_t e0 = seq_status().ends;
        seq_toggle_play();
        run(7.5);
        const bool still = seq_status().playing;
        run(1.0);
        check(still && !seq_status().playing && seq_status().ends == e0 + 1, "a 2-bar row x2 lasts 4 bars (8 s), then stops");
        seq_set_song(false);
    }

    // --- arpeggiator ---------------------------------------------------------
    {
        SeqPattern empty;
        memset(&empty, 0, sizeof(empty));
        seq_put_slot(0, &empty);
        seq_select_slot(0);
        seq_set_song(false);
        seq_set_bpm(120);  // a 16th is 0.125 s
        SeqArp arp = {true, ARP_UP, ARP_16TH, 1};
        seq_set_arp(&arp);
        const int chord[3] = {64, 60, 67};  // held in any order
        // Stopped: it free-runs at the tempo, low to high.
        s_nhits = 0;
        seq_arp_hold(1, chord, 3);
        run(0.55);
        seq_arp_hold(1, NULL, 0);
        run(0.3);
        int got[8], ng = 0;
        long when[8];
        for (int i = 0; i < s_nhits && ng < 8; i++)
            if (s_hits[i].vel > 0 && s_hits[i].synth == part_synth(1)) {
                when[ng] = s_hits[i].t;
                got[ng++] = s_hits[i].note;
            }
        printf("  arp up: %d %d %d %d %d\n", got[0], got[1], got[2], got[3], got[4]);
        check(ng == 5 && got[0] == 60 && got[1] == 64 && got[2] == 67 && got[3] == 60 && labs(when[1] - when[0] - 5512) < 200,
              "the arpeggiator walks the held chord upwards, in 16ths");
        int offs = 0;
        for (int i = 0; i < s_noffs; i++) offs += s_offs[i].synth == part_synth(1);
        check(offs >= 5, "...and each of its notes ends");

        // Down over two octaves.
        arp = (SeqArp){true, ARP_DOWN, ARP_8TH, 2};
        seq_set_arp(&arp);
        s_nhits = 0;
        seq_arp_hold(1, chord, 3);
        run(1.4);
        seq_arp_hold(1, NULL, 0);
        run(0.3);
        ng = 0;
        for (int i = 0; i < s_nhits && ng < 8; i++)
            if (s_hits[i].vel > 0 && s_hits[i].synth == part_synth(1)) {
                when[ng] = s_hits[i].t;
                got[ng++] = s_hits[i].note;
            }
        check(ng >= 5 && got[0] == 79 && got[1] == 76 && got[2] == 72 && got[3] == 67 && labs(when[1] - when[0] - 11025) < 200,
              "DOWN over 2 octaves in 8ths starts at the top");

        // Playing and recording: it lands on the steps and goes into the pattern.
        arp = (SeqArp){true, ARP_UP, ARP_16TH, 1};
        seq_set_arp(&arp);
        seq_set_recording(true);
        seq_toggle_play();
        run(0.01);
        seq_arp_hold(2, chord, 3);
        run(0.9);
        seq_arp_hold(2, NULL, 0);
        seq_set_recording(false);
        seq_toggle_play();
        run(0.3);
        SeqPattern rec;
        seq_get_slot(0, &rec);
        int on_part3 = 0, steps_ok = 1;
        for (int i = 0; i < rec.count; i++)
            if (rec.notes[i].part == 2) {
                on_part3++;
                steps_ok &= rec.notes[i].len == 1;
            }
        printf("  recorded %d arp notes\n", on_part3);
        check(on_part3 >= 6 && steps_ok, "recording, the arpeggio goes into the pattern, a step per note");

        // 1/32: every note it plays is recorded -- the ones between the steps
        // as off-beat notes (SEQ_NOTE_HALF) -- and plays back at the same times.
        seq_put_slot(0, &empty);
        arp = (SeqArp){true, ARP_UP, ARP_32ND, 1};
        seq_set_arp(&arp);
        seq_set_recording(true);
        s_nhits = 0;
        s_clock = 0;
        seq_toggle_play();
        run(0.01);
        seq_arp_hold(2, chord, 3);
        run(0.97);  // 8 steps: 16 notes
        seq_arp_hold(2, NULL, 0);
        seq_set_recording(false);
        seq_toggle_play();
        run(0.1);
        long live[64];
        int nlive = 0;
        for (int i = 0; i < s_nhits && nlive < 64; i++)
            if (s_hits[i].synth == part_synth(2)) live[nlive++] = s_hits[i].t;
        seq_get_slot(0, &rec);
        int on_grid = 0, halves = 0;
        for (int i = 0; i < rec.count; i++)
            if (rec.notes[i].part == 2) (rec.notes[i].flags & SEQ_NOTE_HALF) ? halves++ : on_grid++;
        // Play it back (arp off): the same notes at the same times.
        arp.on = false;
        seq_set_arp(&arp);
        s_nhits = 0;
        s_clock = 0;
        seq_toggle_play();
        run(0.98);
        seq_toggle_play();
        run(0.1);
        int nback = 0, same = 0;
        for (int i = 0; i < s_nhits; i++)
            if (s_hits[i].synth == part_synth(2)) {
                if (nback < nlive && labs(s_hits[i].t - live[nback]) <= BLOCK) same++;
                nback++;
            }
        printf("  1/32: played %d, recorded %d on the steps + %d between, played back %d (%d on time)\n", nlive, on_grid,
               halves, nback, same);
        check(nlive >= 15 && on_grid + halves == nlive && halves >= on_grid - 1 && same == nlive,
              "a 1/32 arpeggio records every note, and plays back the same");
    }

    // --- a crowded step ------------------------------------------------------
    {   // 60 notes on one step (more than the 32 it holds at once): every note
        // that starts also stops; no note-off is ever dropped.
        SeqPattern busy;
        memset(&busy, 0, sizeof(busy));
        busy.len = SEQ_STEPS;
        for (int i = 0; i < 60; i++) busy.notes[busy.count++] = (SeqRollNote){0, (uint8_t)(30 + i), 1, 200, 0, 0};
        for (int s = 0; s < SEQ_STEPS; s++) busy.drums[s] = 0xFF;
        seq_put_slot(0, &busy);
        seq_select_slot(0);
        seq_set_bpm(120);
        s_nhits = s_noffs = 0;
        seq_toggle_play();
        run(0.3);
        seq_toggle_play();
        run(0.1);
        int on[128] = {0}, ons = 0, unmatched = 0;
        for (int i = 0; i < s_nhits; i++)
            if (s_hits[i].synth == part_synth(0)) on[s_hits[i].note]++, ons++;
        for (int i = 0; i < s_noffs; i++)
            if (s_offs[i].synth == part_synth(0)) on[s_offs[i].note]--;
        for (int p = 0; p < 128; p++) unmatched += on[p] > 0;
        printf("  crowded step: %d note-ons, %d left without an off\n", ons, unmatched);
        check(ons == 60 && unmatched == 0, "a step of 60 notes plays them all, and stops them all");
    }

    printf(fails ? "\n%d sequencer problem(s)\n" : "\nall sequencer OK\n", fails);
    return fails ? 1 : 0;
}
