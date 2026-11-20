// sound_probe -- loads every Vibe Machine sound into a real AMY instance on
// the build machine, plays a note, and checks that
//   1. it makes sound,
//   2. each macro audibly changes the result (level or brightness).
// Exits non-zero on failure, so CI can run it.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "amy.h"
#include "sounds.h"

#define SECONDS_TO_BLOCKS(s) ((int)((s) * AMY_SAMPLE_RATE / AMY_BLOCK_SIZE))

typedef struct {
    double rms, bright;  // bright: mean |slope| / mean |x|, a crude spectral centroid
} Measure;

static Measure render(double seconds) {
    double sq = 0, absx = 0, slope = 0;
    long n = 0;
    int prev = 0;
    for (int b = 0; b < SECONDS_TO_BLOCKS(seconds); b++) {
        const int16_t *blk = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE; i++) {
            int x = (blk[2 * i] + blk[2 * i + 1]) / 2;
            sq += (double)x * x;
            absx += fabs((double)x);
            slope += fabs((double)(x - prev));
            prev = x;
            n++;
        }
    }
    Measure m = {sqrt(sq / n) / 32768.0, absx > 0 ? slope / absx : 0};
    return m;
}

static void wire(const char *msg) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", msg);
    amy_add_message(buf);
}

static void note(int n, float vel) {
    amy_event e = amy_default_event();
    e.synth = 1;
    e.midi_note = (float)n;
    e.velocity = vel;
    amy_add_event(&e);
}

static void load(int i) {
    const Sound *s = sound_get(i);
    if (s->kind == SOUND_BANK) {
        amy_event e = amy_default_event();
        e.synth = 1;
        e.num_voices = (uint16_t)s->voices;
        e.patch_number = (uint16_t)s->bank_first;
        amy_add_event(&e);
    } else {
        char buf[512];
        snprintf(buf, sizeof(buf), "i1iv%du%s", s->voices, s->patch);
        amy_add_message(buf);
    }
    char lvl[32];
    snprintf(lvl, sizeof(lvl), "i1iV%.2f", s->level);
    wire(lvl);
    for (int m = 0; m < SOUND_MACROS; m++) {
        char msg[128];
        if (sound_macro_wire(i, m, s->macro_default[m], msg, sizeof(msg))) wire(msg);
    }
    render(0.05);
}

static Measure play(double hold) {
    note(57, 0.8f);
    Measure m = render(hold);
    note(57, 0.0f);
    render(1.2);  // let it ring out before the next test
    return m;
}

int main(void) {
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE;
    c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0;
    c.platform.multithread = 0;
    c.features.default_synths = 1;
    amy_start(c);

    int failures = 0;
    printf("%-9s %8s %8s   macro effects (lo -> hi: rms, brightness)\n", "sound", "rms", "bright");
    for (int i = 0; i < sound_count(); i++) {
        const Sound *s = sound_get(i);
        load(i);
        Measure m = play(0.5);
        bool silent = m.rms < 0.002;
        printf("%-9s %8.4f %8.3f  %s", s->name, m.rms, m.bright, silent ? "SILENT! " : "");
        failures += silent;
        for (int k = 0; k < SOUND_MACROS && s->kind == SOUND_CUSTOM; k++) {
            char msg[128];
            sound_macro_wire(i, k, 0.05f, msg, sizeof(msg));
            wire(msg);
            Measure lo = play(0.4);
            sound_macro_wire(i, k, 0.95f, msg, sizeof(msg));
            wire(msg);
            Measure hi = play(0.4);
            sound_macro_wire(i, k, s->macro_default[k], msg, sizeof(msg));
            wire(msg);
            double drms = fabs(hi.rms - lo.rms) / fmax(fmax(hi.rms, lo.rms), 1e-6);
            double dbr = fabs(hi.bright - lo.bright) / fmax(fmax(hi.bright, lo.bright), 1e-6);
            bool dead = drms < 0.03 && dbr < 0.03;
            printf(" | %s %.3f->%.3f, %.2f->%.2f%s", s->macro_name[k], lo.rms, hi.rms, lo.bright, hi.bright,
                   dead ? " NO EFFECT!" : "");
            failures += dead;
        }
        printf("\n");
    }
    amy_stop();
    printf(failures ? "\n%d problem(s)\n" : "\nall sounds OK\n", failures);
    return failures ? 1 : 0;
}
