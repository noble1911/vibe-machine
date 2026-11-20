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
    a.sound = 4;
    a.patch = 77;
    a.macro[0] = 0.25f;
    a.macro[1] = 0.75f;
    for (int i = 0; i < PARAM_COUNT; i++) a.params[i] = 0.1f * (i + 1);
    a.octave = -2;
    a.bpm = 137;
    for (int i = 0; i < SEQ_SLOTS; i++) dice_groove(&a.slots[i], 12345u + i, 0);
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
        check(loaded && old.bpm == 99 && strcmp(old.name, "OLD SONG") == 0 && p->count == 4 && chord_at_4 == 3 &&
                  d_at_9 == 1 && p->accents == 0x0101 && p->drums[0] == 1,
              "a song saved before the piano roll loads with its notes");
    }

    // --- WAV ------------------------------------------------------------------
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
