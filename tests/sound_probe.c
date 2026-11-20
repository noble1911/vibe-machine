// sound_probe -- loads every Vibe Machine sound into a real AMY instance on
// the build machine, plays a note, and checks that
//   1. it makes sound,
//   2. each macro audibly changes the result (level or brightness).
// Exits non-zero on failure, so CI can run it.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amy.h"
#include "drawwave.h"
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

// Pitch by autocorrelation over `seconds` (after a short settle): the
// first lag whose correlation is within 10% of the best.
static double pitch_of(double seconds) {
    render(0.01);
    static float x[16384];
    int n = 0;
    for (int b = 0; b < SECONDS_TO_BLOCKS(seconds) && n + AMY_BLOCK_SIZE <= 16384; b++) {
        const int16_t *blk = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE; i++) x[n++] = (blk[2 * i] + blk[2 * i + 1]) / 65536.0f;
    }
    const int minlag = AMY_SAMPLE_RATE / 2000, maxlag = AMY_SAMPLE_RATE / 50, len = n - maxlag - 1;
    if (len < 64) return 0;
    static double r[2048];
    double best = -1;
    for (int L = minlag; L <= maxlag && L < 2048; L++) {
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < len; i++) {
            sxy += x[i] * x[i + L];
            sxx += x[i] * x[i];
            syy += x[i + L] * x[i + L];
        }
        r[L] = sxy / sqrt(sxx * syy + 1e-20);
        if (r[L] > best) best = r[L];
    }
    for (int L = minlag + 1; L < maxlag && L < 2047; L++)
        if (r[L] > 0.9 * best && r[L] >= r[L - 1] && r[L] >= r[L + 1]) {
            const double a = r[L - 1], b = r[L], c = r[L + 1], d = a - 2 * b + c;  // parabolic refinement
            return AMY_SAMPLE_RATE / (L + (d != 0 ? 0.5 * (a - c) / d : 0));
        }
    return 0;
}

// Load sound i on synth 1 with an EDIT layer, as the app does: the patch
// with the edit's structure, the macros, then the edit's live commands.
static void load_edit(int i, const SynthEdit *e) {
    const Sound *s = sound_get(i);
    char buf[600];
    if (s->kind == SOUND_BANK) {
        amy_event ev = amy_default_event();
        ev.synth = 1;
        ev.num_voices = (uint16_t)sound_edit_voices(i, e);
        ev.patch_number = (uint16_t)s->bank_first;
        amy_add_event(&ev);
    } else {
        char body[400];
        sound_edit_patch(i, e, body, sizeof(body));
        snprintf(buf, sizeof(buf), "i1iv%du%s", sound_edit_voices(i, e), body);
        amy_add_message(buf);
    }
    snprintf(buf, sizeof(buf), "i1iV%.2f", s->level);
    wire(buf);
    for (int m = 0; m < SOUND_MACROS; m++)
        if (sound_macro_wire(i, m, s->macro_default[m], 1, buf, sizeof(buf))) wire(buf);
    char cmds[16][64];
    const int n = sound_edit_wire(i, e, 1, &cmds[0][0], 64, 16);
    for (int k = 0; k < n; k++) wire(cmds[k]);
    render(0.05);
}

static void fresh_edit(int i, SynthEdit *e) {
    float d[ED_PARAMS];
    sound_edit_defaults(i, d);
    edit_reset(e, d);
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
    {   // the DRAW sound's wave for part 1 (preset DRAW_PRESET): a saw
        DrawWave w;
        draw_preset(&w, 1);
        static int16_t cyc[DRAW_CYCLE];
        draw_render(&w, cyc);
        int16_t *ram = pcm_load(DRAW_PRESET, 2 * DRAW_CYCLE, 44100, 1, 60.0f, 0, 0);  // as engine_load_wave
        memcpy(ram, cyc, sizeof(cyc));
        memcpy(ram + DRAW_CYCLE, cyc, sizeof(cyc));
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
    // --- EDIT ---------------------------------------------------------------
    printf("\nEDIT\n");
    {
        const int saw = 6, draw = sound_draw_index(), pluck = 2;  // SUPERSAW, DRAW, PLUCK
        SynthEdit e;
        {   // let part 2's PLUCK (above) stop ringing
            amy_event off = amy_default_event();
            off.synth = 2;
            off.midi_note = 57;
            off.velocity = 0.0f;
            amy_add_event(&off);
            render(3.0);
        }
#define CHECK(cond, ...)                                    \
    do {                                                    \
        const bool ok_ = (cond);                            \
        printf(__VA_ARGS__);                                \
        printf(" %s\n", ok_ ? "ok" : "FAIL");              \
        failures += !ok_;                                   \
    } while (0)
        // An untouched edit sounds exactly like the plain sound.
        load(saw);
        Measure plain = play(0.4);
        fresh_edit(saw, &e);
        load_edit(saw, &e);
        Measure same = play(0.4);
        CHECK(fabs(plain.rms - same.rms) < 0.1 * plain.rms && fabs(plain.bright - same.bright) < 0.1 * plain.bright,
              "  untouched: rms %.4f / %.4f, bright %.3f / %.3f", plain.rms, same.rms, plain.bright, same.bright);
        // CUTOFF darkens and brightens.
        fresh_edit(saw, &e);
        edit_set(&e, ED_CUTOFF, 0.15f);
        load_edit(saw, &e);
        Measure dark = play(0.4);
        edit_set(&e, ED_CUTOFF, 0.95f);
        load_edit(saw, &e);
        Measure bright = play(0.4);
        CHECK(dark.bright < bright.bright * 0.6, "  CUTOFF low -> high: bright %.3f -> %.3f", dark.bright, bright.bright);
        // A sound with no filter (WAVY) gets one when CUTOFF is touched.
        fresh_edit(8, &e);
        edit_set(&e, ED_CUTOFF, 0.1f);
        load_edit(8, &e);
        Measure wd = play(0.4);
        edit_set(&e, ED_CUTOFF, 1.0f);
        load_edit(8, &e);
        Measure wb = play(0.4);
        CHECK(wd.bright < wb.bright * 0.6, "  WAVY (no filter) CUTOFF low -> high: bright %.3f -> %.3f", wd.bright, wb.bright);
        // A slow ATTACK: the first 60 ms are much quieter.
        fresh_edit(pluck, &e);
        load_edit(pluck, &e);
        note(57, 0.8f);
        Measure fast = render(0.06);
        note(57, 0.0f);
        render(1.5);
        edit_set(&e, ED_ATTACK, 0.85f);
        load_edit(pluck, &e);
        note(57, 0.8f);
        Measure slow = render(0.06);
        note(57, 0.0f);
        render(2.0);
        CHECK(slow.rms < fast.rms * 0.4, "  ATTACK slow: first 60 ms rms %.4f -> %.4f", fast.rms, slow.rms);
        // LFO on VOLUME: the level wobbles (block-to-block rms varies a lot more).
        double wob[2];
        for (int on = 0; on < 2; on++) {
            fresh_edit(draw, &e);
            if (on) {
                e.lfo_target = LFO_AMP;
                edit_set(&e, ED_LFO_RATE, 0.6f);
                edit_set(&e, ED_LFO_DEPTH, 0.9f);
            }
            load_edit(draw, &e);
            note(57, 0.8f);
            render(0.1);
            double lo = 1e9, hi = 0;
            for (int k = 0; k < 20; k++) {
                Measure m = render(0.03);
                lo = m.rms < lo ? m.rms : lo;
                hi = m.rms > hi ? m.rms : hi;
            }
            note(57, 0.0f);
            render(1.0);
            wob[on] = hi > 0 ? lo / hi : 1;
        }
        CHECK(wob[1] < wob[0] - 0.3, "  LFO on VOLUME: quietest/loudest %.2f -> %.2f", wob[0], wob[1]);
        // DRAW plays in tune, and pitch bend moves it +2 semitones. (A sine
        // now: a saw's jump rings, which the crossing count would miscount.)
        {
            DrawWave w;
            draw_preset(&w, 0);
            static int16_t cyc[DRAW_CYCLE];
            draw_render(&w, cyc);
            int16_t *ram = pcm_load(DRAW_PRESET, 2 * DRAW_CYCLE, 44100, 1, 60.0f, 0, 0);
            memcpy(ram, cyc, sizeof(cyc));
            memcpy(ram + DRAW_CYCLE, cyc, sizeof(cyc));
        }
        fresh_edit(draw, &e);
        load_edit(draw, &e);
        note(57, 0.8f);
        const double hz = pitch_of(0.5);
        amy_event b = amy_default_event();
        b.pitch_bend = 2.0f / 12.0f;
        amy_add_event(&b);
        const double bent = pitch_of(0.5);
        b.pitch_bend = 0.0f;
        amy_add_event(&b);
        note(57, 0.0f);
        render(1.0);
        CHECK(fabs(hz - 220) < 6, "  DRAW A3 plays at %.1f Hz", hz);
        CHECK(fabs(bent - 246.9) < 7, "  bend +2: %.1f Hz", bent);
        // MONO: a chord comes out as one note; UNISON x3 still sounds, on fewer voices.
        fresh_edit(draw, &e);
        load_edit(draw, &e);
        note(57, 0.8f);
        note(61, 0.8f);
        note(64, 0.8f);
        Measure poly = render(0.3);
        for (int k = 57; k <= 64; k++) note(k, 0.0f);
        render(1.0);
        e.mono = 1;
        load_edit(draw, &e);
        note(57, 0.8f);
        note(61, 0.8f);
        note(64, 0.8f);
        Measure mono = render(0.3);
        for (int k = 57; k <= 64; k++) note(k, 0.0f);
        render(1.0);
        CHECK(sound_edit_voices(draw, &e) == 1 && mono.rms < poly.rms * 0.75, "  MONO: chord rms %.4f -> %.4f", poly.rms,
              mono.rms);
        fresh_edit(draw, &e);
        e.unison = 3;
        edit_set(&e, ED_DETUNE, 0.5f);
        load_edit(draw, &e);
        Measure uni = play(0.3);
        CHECK(sound_edit_voices(draw, &e) == 1 && uni.rms > 0.01, "  UNISON x3: %d voice(s), rms %.4f",
              sound_edit_voices(draw, &e), uni.rms);
        // GLIDE in MONO: the second note slides up from the first.
        const int gs = getenv("GLIDE_SOUND") ? atoi(getenv("GLIDE_SOUND")) : draw;
        fresh_edit(gs, &e);
        e.mono = 1;
        edit_set(&e, ED_GLIDE, 0.75f);
        load_edit(gs, &e);
        note(45, 0.8f);  // A2, 110 Hz (held long enough to arrive: it glides from wherever the osc last was)
        const double start = pitch_of(0.1);
        render(2.0);
        note(57, 0.8f);  // A3: half-way through the glide it's still on its way
        const double mid = pitch_of(0.1);  // GLIDE 0.75 is a ~430 ms time constant
        render(2.5);
        const double end = pitch_of(0.3);
        note(57, 0.0f);
        render(1.0);
        (void)start;
        CHECK(mid > 111 && mid < 190 && fabs(end - 220) < 8, "  GLIDE A2 -> A3: %.0f Hz on the way, %.0f Hz arrived", mid, end);
        // Every CUSTOM sound takes a full edit (LFO, filter, unison) and still sounds.
        for (int i = 0; i < sound_count(); i++) {
            fresh_edit(i, &e);
            edit_mutate(&e, 0.2f, 99u + i);
            edit_set(&e, ED_LFO_DEPTH, 0.5f);
            e.unison = 2;
            load_edit(i, &e);
            Measure m = play(0.4);
            CHECK(m.rms > 0.004 && m.rms < 0.5, "  %-8s edited: rms %.4f", sound_get(i)->name, m.rms);
        }
#undef CHECK
    }
    amy_stop();
    printf(failures ? "\n%d problem(s)\n" : "\nall sounds OK\n", failures);
    return failures ? 1 : 0;
}
