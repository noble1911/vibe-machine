// logic_probe -- host checks for the pure-logic modules: save files and
// the dice groove generator.
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include "dice.h"
#include "sampler.h"
#include "wav.h"
#include "storage.h"

static int fails;
static void check(int ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void) {
    // --- storage round trip -------------------------------------------------
    SaveState a;
    memset(&a, 0, sizeof(a));
    a.theme = 6;
    for (int k = 0; k < PARTS; k++) {  // three parts, three sounds
        a.parts[k].sound = 2 + k;
        a.parts[k].patch = 70 + k;
        a.parts[k].macro[0] = 0.25f + 0.1f * k;
        a.parts[k].macro[1] = 0.75f - 0.1f * k;
    }
    a.part = 2;
    a.muted = 0x2 | (1u << PARTS);  // part 2 and the drums
    for (int k = 0; k < MIX_STRIPS; k++) {  // a mixer with everything moved
        a.mix[k].level = 0.2f + 0.2f * k;
        for (int i = 0; i < MIX_KNOBS; i++) a.mix[k].knob[i] = 0.1f * (k + 1) + 0.05f * i;
    }
    a.solo = 0x4;
    const int8_t srcs[TAPE_TRACKS] = {TAPE_SRC_ALL, MIX_DRUMS, 0, 2};
    memcpy(a.tape_source, srcs, sizeof(srcs));
    a.sample_edit = (SampleEdit){0.125f, 0.875f, 8};
    a.arr.count = 3;
    a.arr.loop = true;
    a.arr.rows[0] = (SeqArrRow){0, 4, 0};
    a.arr.rows[1] = (SeqArrRow){1, 2, 1u << PARTS};
    a.arr.rows[2] = (SeqArrRow){7, 8, 0x5};
    a.tape_muted = 0x9;
    a.key_root = 9;   // A
    a.key_scale = 6;  // minor pentatonic
    a.arp = (SeqArp){true, ARP_UPDOWN, ARP_32ND, 2};
    a.slots[2].fx[5] = 3;  // a stutter on step 6 of slot C
    a.slots[2].fx_x[5] = 200;
    a.slots[2].fx_y[5] = 40;
    for (int i = 0; i < PARAM_COUNT; i++) a.params[i] = 0.1f * (i + 1);
    a.octave = -2;
    a.bpm = 137;
    for (int i = 0; i < SEQ_SLOTS; i++) dice_groove(&a.slots[i], 12345u + i, 0);
    a.slots[3].len = 32;  // a two-bar pattern
    a.slots[3].drums[20] = 0x5;
    a.slots[3].accents |= 1ull << 40;
    for (int i = 0; i < a.slots[0].count; i++) a.slots[0].notes[i].part = (uint8_t)(i % PARTS);
    a.slot = 5;
    a.song = true;
    a.swing = 0.3f;
    strcpy(a.name, "NEON GECKO 7");
    a.unsaved = true;  // round-trips: unsaved edits survive a restart
    a.tape_changed = 0x5;  // tracks 1 and 3 changed since the song was loaded
    a.mic_changed = true;
    for (int k = 0; k < FX_COUNT; k++) a.fx_x[k] = a.fx_y[k] = 0.05f * k;

    const char *path = "build/state_test.vms";
    check(storage_save(path, &a), "save writes a file");
    SaveState b;
    memset(&b, 0, sizeof(b));
    check(storage_load(path, &b) && memcmp(&a, &b, sizeof(a)) == 0, "load restores every field exactly");

    FILE *f = fopen(path, "r+b");
    fseek(f, 20, SEEK_SET);
    fputc(0x5A, f);
    fclose(f);
    SaveState c = b;
    check(!storage_load(path, &c) && memcmp(&c, &b, sizeof(b)) == 0, "a damaged file is rejected and changes nothing");
    check(!storage_load("build/does_not_exist.vms", &c), "a missing file is reported");

    {   // The biggest song there can be still fits the file.
        SaveState big = a;
        for (int i = 0; i < SEQ_SLOTS; i++) {
            big.slots[i].len = SEQ_STEPS_MAX;
            big.slots[i].accents = ~0ull;
            big.slots[i].count = SEQ_NOTES;
            for (int n = 0; n < SEQ_NOTES; n++) big.slots[i].notes[n] = (SeqRollNote){(uint8_t)(n % 64), 60, 1, 200, 0};
            for (int st = 0; st < SEQ_STEPS_MAX; st++) {
                big.slots[i].drums[st] = 0xFF;
                big.slots[i].fx[st] = 1 + st % 4;
                big.slots[i].fx_x[st] = big.slots[i].fx_y[st] = (uint8_t)(st * 16);
            }
        }
        big.arr.count = SEQ_ARR_ROWS;
        for (int i = 0; i < SEQ_ARR_ROWS; i++) big.arr.rows[i] = (SeqArrRow){(uint8_t)(i % 8), 8, 0xF};
        SaveState back;
        check(storage_save("build/big.vms", &big) && storage_load("build/big.vms", &back) &&
                  memcmp(&big, &back, sizeof(big)) == 0,
              "the biggest possible song saves and loads (fits the file)");
    }

    // --- dice -----------------------------------------------------------------
    int one = 1, backbeat = 0, in_key = 1, busy = 0;
    static const int major[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    for (uint32_t seed = 1; seed <= 200; seed++) {
        SeqPattern p;
        dice_groove(&p, seed * 7919u, 0);
        one &= (p.drums[0] & 1) != 0;
        backbeat += ((p.drums[4] >> 1) & 1) + ((p.drums[12] >> 1) & 1);
        int hits = 0;
        for (int s = 0; s < SEQ_STEPS; s++) {
            for (int l = 0; l < SEQ_LANES; l++) hits += (p.drums[s] >> l) & 1;
            (void)s;
        }
        busy += hits >= 6 && hits <= 60;
        for (int i = 0; i < p.count; i++) in_key &= major[p.notes[i].pitch % 12];
    }
    check(one, "every groove lands the kick on the one");
    check(backbeat > 300, "snares sit on beats 2 and 4 most of the time");
    check(in_key, "every synth note is in C major");
    check(busy == 200, "grooves are neither empty nor wall-to-wall");

    // --- sampler ---------------------------------------------------------------
    static int16_t take[SAMPLER_MAX_FRAMES];
    memset(take, 0, sizeof(take));
    int lead = SAMPLER_RATE / 2;  // half a second of silence, then the boing at a low level
    int n = sampler_boing(take + lead, SAMPLER_MAX_FRAMES - lead);
    for (int i = lead; i < lead + n; i++) take[i] /= 8;
    int len = sampler_tidy(take, lead + n);
    int peak = 0, first = -1;
    for (int i = 0; i < len; i++) {
        peak = abs(take[i]) > peak ? abs(take[i]) : peak;
        if (first < 0 && abs(take[i]) > 2000) first = i;
    }
    printf("  tidy: %d -> %d frames, peak %d, first loud frame %d\n", lead + n, len, peak, first);
    check(len > 0 && len < lead + n, "a take loses its leading silence");
    check(peak > 28000 && peak < 30000, "a quiet take is normalised to ~0.9 of full scale");
    check(first >= 0 && first < SAMPLER_RATE / 50, "the sound starts within 20 ms of the key press");
    memset(take, 0, sizeof(take));
    check(sampler_tidy(take, SAMPLER_RATE) == 0, "a silent take is refused");

    // --- an older save (format 5, one note-or-chord per step) converts ---------
    {
        uint8_t pay[2048];
        size_t n = 0;
#define PUT(p, len) (memcpy(pay + n, (p), (len)), n += (len))
        int32_t ints[5] = {2, 3, 0, 1, 99};  // theme, sound, patch, octave, bpm
        float f8[2 + PARAM_COUNT + 2 * FX_COUNT] = {0};
        PUT(ints, sizeof(ints));
        PUT(f8, sizeof(f8));
        int32_t slot = 0;
        uint8_t song = 0;
        float swing = 0.25f;
        PUT(&slot, 4);
        PUT(&song, 1);
        PUT(&swing, 4);
        for (int sl = 0; sl < SEQ_SLOTS; sl++) {
            uint8_t drums[SEQ_STEPS] = {0};
            if (sl == 0) drums[0] = 1;
            PUT(drums, sizeof(drums));
            for (int st = 0; st < SEQ_STEPS; st++) {
                uint8_t cell[5] = {0, 0, 0, 0, 0};  // key, count, notes[3]
                if (sl == 0 && st == 4) cell[1] = 3, cell[2] = 60, cell[3] = 64, cell[4] = 67;  // a chord
                if (sl == 0 && st == 9) cell[1] = 1, cell[2] = 62;
                PUT(cell, 5);
            }
            uint16_t acc = sl == 0 ? 0x0101 : 0;
            PUT(&acc, 2);
        }
        char name[17] = "OLD SONG";
        PUT(name, 17);
        uint8_t tail[3] = {0, 0, 0};  // unsaved, tape_changed, mic_changed
        PUT(tail, 3);
#undef PUT
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < n; i++) h = (h ^ pay[i]) * 16777619u;
        FILE *f = fopen("build/v5.vms", "wb");
        uint16_t head[2] = {5, (uint16_t)n};
        fwrite("VIBE", 1, 4, f);
        fwrite(head, sizeof(head), 1, f);
        fwrite(pay, 1, n, f);
        fwrite(&h, 4, 1, f);
        fclose(f);
        SaveState old;
        bool loaded = storage_load("build/v5.vms", &old);
        const SeqPattern *p = &old.slots[0];
        int chord_at_4 = 0, d_at_9 = 0;
        for (int i = 0; i < p->count; i++) {
            if (p->notes[i].step == 4 && p->notes[i].len == 1) chord_at_4++;
            if (p->notes[i].step == 9 && p->notes[i].pitch == 62) d_at_9++;
        }
        check(loaded && old.parts[0].sound == 3 && old.parts[1].sound == -1 && old.part == 0,
              "an older song keeps its sound on part 1; the other parts get defaults");
        check(loaded && old.mix[MIX_DRUMS].level == MIX_LEVEL_UNITY && old.mix[0].knob[MIX_PAN] == 0.5f && old.solo == 0 &&
                  old.tape_source[0] == TAPE_SRC_ALL && old.tape_source[3] == TAPE_SRC_ALL,
              "an older song gets a flat mixer and tapes that record everything");
        check(loaded && old.bpm == 99 && strcmp(old.name, "OLD SONG") == 0 && p->count == 4 && chord_at_4 == 3 &&
                  d_at_9 == 1 && p->accents == 0x0101 && p->drums[0] == 1,
              "a song saved before the piano roll loads with its notes");
    }

    // --- keys and scales ------------------------------------------------------
    {
        music_set_key(9, 1);  // A minor
        int n[3], ok = 1;
        static const int want[8] = {69, 71, 72, 74, 76, 77, 79, 81};
        for (int k = 0; k < 8; k++) {
            music_key_notes(k, PLAY_NOTE, 0, n);
            ok &= n[0] == want[k];
        }
        check(ok, "A minor: the keys play A B C D E F G A");
        static const char *const chords[8] = {"Am", "Bdim", "C", "Dm", "Em", "F", "G", "Am"};
        ok = 1;
        for (int k = 0; k < 8; k++) ok &= strcmp(music_key_label(k, PLAY_CHORD), chords[k]) == 0;
        check(ok, "A minor's chords are named Am Bdim C Dm Em F G Am");
        check(music_in_scale(70) == false && music_in_scale(72) && music_key_of_pitch(74) == 3 && music_key_of_pitch(70) == -1,
              "the roll and the key flashes follow the scale");
        music_set_key(4, 6);  // E minor pentatonic: E G A B D, then on up
        static const int penta[8] = {64, 67, 69, 71, 74, 76, 79, 81};
        ok = 1;
        for (int k = 0; k < 8; k++) {
            music_key_notes(k, PLAY_NOTE, 0, n);
            ok &= n[0] == penta[k];
        }
        check(ok && strcmp(music_key_label(5, PLAY_NOTE), "E+") == 0, "a 5-note scale carries on up the keys");
        music_key_notes(0, PLAY_NOTE, -1, n);
        check(n[0] == 52 && music_degree(52) == -5 && music_degree(63) == -1, "octaves down count degrees down");
        check(sampler_slice_of_pitch(64, 4) == 0 && sampler_slice_of_pitch(67, 4) == 1 && sampler_slice_of_pitch(76, 4) == 1,
              "CHOP slices follow the scale's keys");
        music_set_key(0, 0);
    }

    // --- sample edits ---------------------------------------------------------
    {
        SampleEdit e = {0.25f, 0.75f, 4};
        int first, n = sampler_region(&e, 1000, -1, &first);
        check(first == 250 && n == 500, "START..END picks the middle of the take");
        int f1, n1 = sampler_region(&e, 1000, 1, &f1), f3, n3 = sampler_region(&e, 1000, 3, &f3);
        check(f1 == 375 && n1 == 125 && f3 + n3 == 750, "CHOP 4 cuts the region into four equal slices");
        static const int pitches[] = {60, 62, 64, 65, 67, 69, 71, 72, 61, 59};
        static const int want[] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 3};  // B3 is one step below C4: the last slice
        int right = 0;
        for (int i = 0; i < 10; i++) right += sampler_slice_of_pitch(pitches[i], 4) == want[i];
        check(right == 10, "the keys walk through the slices, wrapping; sharps take the slice below");
        SampleEdit bad = {0.9f, 0.1f, 1};
        sampler_edit_clamp(&bad);
        check(bad.start < bad.end && bad.slices == 0, "a back-to-front edit is put right");
        static int16_t in[2205], out[4000];
        for (int i = 0; i < 2205; i++) in[i] = (int16_t)(i * 10);
        int m = sampler_resample(in, 2205, 22050, out, 4000, SAMPLER_RATE);
        check(abs(m - 1637) <= 1 && abs(out[m / 2] - in[(int)(m / 2 * 22050.0 / SAMPLER_RATE)]) <= 10,
              "resampling 22.05 kHz tape to the sampler's rate keeps length and shape");
    }

    // --- WAV ------------------------------------------------------------------
    {   // A streamed stereo WAV (the mixdown): blocks in, sizes patched on close.
        WavStream ws;
        bool ok = wav_stream_open(&ws, "build/stream.wav", 44100, 2);
        static int16_t blk[256 * 2];
        for (int b = 0; b < 10 && ok; b++) {
            for (int i = 0; i < 256; i++) blk[2 * i] = blk[2 * i + 1] = (int16_t)(b * 256 + i);
            ok = wav_stream_write(&ws, blk, 256);
        }
        ok &= wav_stream_close(&ws);
        static int16_t back[4000];
        int srate = 0, sn = wav_read("build/stream.wav", back, 4000, &srate);
        check(ok && sn == 2560 && srate == 44100 && back[0] == 0 && back[2559] == 2559,
              "a streamed stereo WAV reads back whole, with the right length");
    }
    static int16_t wav_in[4000], wav_out[4000];
    for (int i = 0; i < 4000; i++) wav_in[i] = (int16_t)((i * 37) % 30000 - 15000);
    int rate = 0;
    check(wav_write("build/test.wav", wav_in, 4000, 22050), "a WAV file is written");
    int got = wav_read("build/test.wav", wav_out, 4000, &rate);
    check(got == 4000 && rate == 22050 && memcmp(wav_in, wav_out, sizeof(wav_in)) == 0, "it reads back sample-exact at its rate");
    check(wav_read("build/missing.wav", wav_out, 4000, &rate) < 0, "a missing WAV is reported");

    printf(fails ? "\n%d logic problem(s)\n" : "\nall logic OK\n", fails);
    return fails ? 1 : 0;
}
