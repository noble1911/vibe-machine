// logic_probe -- host checks for the pure-logic modules: save files and
// the dice groove generator.
#include <stdio.h>
#include <string.h>

#include "dice.h"
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
    dice_groove(&a.pattern, 12345, 0);
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
            for (int i = 0; i < p.synth[s].count; i++) in_key &= major[p.synth[s].notes[i] % 12];
        }
        busy += hits >= 6 && hits <= 60;
    }
    check(one, "every groove lands the kick on the one");
    check(backbeat > 300, "snares sit on beats 2 and 4 most of the time");
    check(in_key, "every synth note is in C major");
    check(busy == 200, "grooves are neither empty nor wall-to-wall");

    printf(fails ? "\n%d logic problem(s)\n" : "\nall logic OK\n", fails);
    return fails ? 1 : 0;
}
