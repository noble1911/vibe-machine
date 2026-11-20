// sound_probe -- loads every Vibe Machine sound into a real AMY instance on
// the build machine, plays a note, and checks that
//   1. it makes sound,
//   2. each macro audibly changes the result (level or brightness).
// Exits non-zero on failure, so CI can run it.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "amy.h"
#include "sampler.h"
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
        if (sound_macro_wire(i, m, s->macro_default[m], 1, msg, sizeof(msg))) wire(msg);
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
    {   // the MIC sound plays this memory preset (the app loads it at boot)
        static int16_t boing[SAMPLER_RATE];
        int n = sampler_boing(boing, SAMPLER_RATE);
        int16_t *ram = pcm_load(SAMPLER_PRESET, (uint32_t)n, SAMPLER_RATE, 1, 60.0f, 0, 0);
        memcpy(ram, boing, (size_t)n * sizeof(int16_t));
    }

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
            sound_macro_wire(i, k, 0.05f, 1, msg, sizeof(msg));
            wire(msg);
            Measure lo = play(0.4);
            sound_macro_wire(i, k, 0.95f, 1, msg, sizeof(msg));
            wire(msg);
            Measure hi = play(0.4);
            sound_macro_wire(i, k, s->macro_default[k], 1, msg, sizeof(msg));
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
    // CHOP: a take that is 220 Hz then 880 Hz, cut in two. Each key plays its
    // own slice at the slice's own pitch (the way engine.c sends the note).
    {
        static int16_t take[SAMPLER_RATE];
        const int n = SAMPLER_RATE;
        for (int i = 0; i < n; i++)
            take[i] = (int16_t)(16000 * sin(2 * M_PI * (i < n / 2 ? 220.0 : 880.0) * i / SAMPLER_RATE));
        SampleEdit ed;
        sampler_edit_default(&ed);
        ed.slices = 2;
        for (int k = 0; k < 2; k++) {
            int first, len = sampler_region(&ed, n, k, &first);
            int16_t *ram = pcm_load(SAMPLER_PRESET + 1 + k, (uint32_t)len, SAMPLER_RATE, 1, 60.0f, 0, 0);
            memcpy(ram, take + first, (size_t)len * sizeof(int16_t));
        }
        load(sound_mic_index());
        double hz[2];
        for (int key = 0; key < 2; key++) {
            const int pitch = key == 0 ? 60 : 62;  // the C and D keys
            amy_event e = amy_default_event();
            e.synth = 1;
            e.midi_note = SAMPLER_SLICE_NOTE;
            e.preset = (uint16_t)(SAMPLER_PRESET + 1 + sampler_slice_of_pitch(pitch, 2));
            e.velocity = 0.8f;
            amy_add_event(&e);
            long crossings = 0, frames = 0;
            int prev = 0;
            for (int b = 0; b < SECONDS_TO_BLOCKS(0.3); b++) {
                const int16_t *blk = amy_simple_fill_buffer();
                for (int i = 0; i < AMY_BLOCK_SIZE; i++, frames++) {
                    int x = blk[2 * i];
                    if (b > 2 && (x >= 0) != (prev >= 0)) crossings++;
                    prev = x;
                }
            }
            hz[key] = crossings / 2.0 / ((frames - 3 * AMY_BLOCK_SIZE) / (double)AMY_SAMPLE_RATE);
            e.velocity = 0.0f;
            amy_add_event(&e);
            render(0.6);
        }
        bool ok = fabs(hz[0] - 220) < 20 && fabs(hz[1] - 880) < 60;
        printf("CHOP: C plays slice 1 at %.0f Hz, D slice 2 at %.0f Hz %s\n", hz[0], hz[1], ok ? "" : "WRONG!");
        failures += !ok;
    }
    // Parts: a sound on synth 2 plays independently of synth 1.
    {
        char buf[512];
        const Sound *pl = sound_get(2);  // PLUCK
        snprintf(buf, sizeof(buf), "i2iv%du%s", pl->voices, pl->patch);
        amy_add_message(buf);
        snprintf(buf, sizeof(buf), "i2iV%.2f", pl->level);
        amy_add_message(buf);
        render(0.05);
        amy_event e = amy_default_event();
        e.synth = 2;
        e.midi_note = 57;
        e.velocity = 0.8f;
        amy_add_event(&e);
        Measure m2 = render(0.5);
        bool ok = m2.rms > 0.01;
        printf("part 2 (PLUCK on synth 2): rms %.4f %s\n", m2.rms, ok ? "" : "SILENT!");
        failures += !ok;
    }
    amy_stop();
    printf(failures ? "\n%d problem(s)\n" : "\nall sounds OK\n", failures);
    return failures ? 1 : 0;
}
