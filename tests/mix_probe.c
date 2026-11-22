// mix_probe -- the MIX page's routing, measured: each part on its own bus,
// strip effects that stay on their strip, mute, level, pan, and the master
// bus's echo still reaching every part.
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "amy.h"
#include "mixer.h"
#include "mix_setup.h"
#include "sounds.h"

#define BLOCKS(s) ((int)((s) * AMY_SAMPLE_RATE / AMY_BLOCK_SIZE))

static int s_fail;
static void check(const char *what, bool ok, const char *fmt, double a, double b) {
    printf("%-58s %s  (", what, ok ? "ok" : "FAIL");
    printf(fmt, a, b);
    printf(")\n");
    if (!ok) s_fail++;
}

typedef struct {
    double rms[2], bright, crest;  // crest = peak / rms: clipping lowers it  // per channel; bright = rms(slope) / rms(x): more highs, higher
    float peak[MIX_STRIPS];
} Measure;

static Measure render(double seconds) {
    Measure m;
    memset(&m, 0, sizeof(m));
    double sq[2] = {0, 0}, xx = 0, slope = 0;
    long n = 0;
    int prev = 0, top = 0;
    for (int k = 0; k < MIX_STRIPS; k++) mixer_peak(k);
    for (int b = 0; b < BLOCKS(seconds); b++) {
        const int16_t *blk = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE; i++) {
            for (int c = 0; c < 2; c++) sq[c] += (double)blk[2 * i + c] * blk[2 * i + c];
            int x = (blk[2 * i] + blk[2 * i + 1]) / 2;
            xx += (double)x * x;
            if (abs(x) > top) top = abs(x);
            slope += (double)(x - prev) * (x - prev);
            prev = x;
            n++;
        }
        for (int k = 0; k < MIX_STRIPS; k++) {
            float p = mixer_peak(k);
            if (p > m.peak[k]) m.peak[k] = p;
        }
    }
    for (int c = 0; c < 2; c++) m.rms[c] = sqrt(sq[c] / n) / 32768.0;
    m.bright = xx > 0 ? sqrt(slope / xx) : 0;
    m.crest = xx > 0 ? top / sqrt(xx / n) : 0;
    return m;
}
static double mono(const Measure *m) { return (m->rms[0] + m->rms[1]) / 2; }
static double db(double a, double b) { return 20.0 * log10((a + 1e-12) / (b + 1e-12)); }

static void wire(const char *msg) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", msg);
    amy_add_message(buf);
}

static MixStrip s_strip[MIX_STRIPS];
static void apply(int k, bool audible) {
    amy_event e = amy_default_event();
    mixer_fill_event(k, &s_strip[k], &e);
    amy_add_event(&e);
    mixer_set_gain(k, &s_strip[k], audible);
}

static void route(int synth, int bus) {
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    e.bus = (uint16_t)bus;
    amy_add_event(&e);
}

static void load(int part, int sound) {
    const Sound *s = sound_get(sound);
    char buf[512];
    snprintf(buf, sizeof(buf), "i%div%du%s", part_synth(part), s->voices, s->patch);
    wire(buf);
    snprintf(buf, sizeof(buf), "i%diV%.3f", part_synth(part), mixer_synth_level(s->level));
    wire(buf);
    route(part_synth(part), part);
}

static void note(int synth, int n, float vel) {
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    e.midi_note = (float)n;
    e.velocity = vel;
    amy_add_event(&e);
}

static Measure play(int synth, int n, double hold) {
    note(synth, n, 0.8f);
    Measure m = render(hold);
    note(synth, n, 0.0f);
    render(1.5);
    return m;
}

int main(void) {
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE;
    c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0;
    c.platform.multithread = 0;
    c.features.default_synths = 1;
    c.max_buses = MIX_BUSES;
    c.amy_external_bus_postprocess_hook = mixer_hook;
    amy_start(c);

    // The master, echo and reverb buses: no echo or reverb yet.
    mix_setup(0.0f, 0.0f);
    route(SYNTH_DRUMS, MIX_DRUMS);
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "i%diV%.3f", SYNTH_DRUMS, mixer_synth_level(1.0f));
        wire(buf);
    }
    load(0, 0);  // JUNO
    load(1, 6);  // SUPERSAW
    load(2, 4);  // BELL
    for (int k = 0; k < MIX_STRIPS; k++) {
        mixer_strip_default(&s_strip[k]);
        apply(k, true);
    }
    render(0.1);

    const int saw = part_synth(1);
    Measure clean = play(saw, 57, 0.6);
    check("part 2 sounds through its strip", mono(&clean) > 0.005 && clean.peak[1] > 0, "rms %.4f peak %.3f",
          mono(&clean), clean.peak[1]);
    check("...and only its strip", clean.peak[0] == 0 && clean.peak[2] == 0 && clean.peak[MIX_DRUMS] == 0,
          "others %.3f, %.3f", clean.peak[0] + clean.peak[2], clean.peak[MIX_DRUMS]);
    Measure drum = play(SYNTH_DRUMS, 36, 0.4);
    check("drums sound through the DRUMS strip", drum.peak[MIX_DRUMS] > 0 && mono(&drum) > 0.002, "peak %.3f rms %.4f",
          drum.peak[MIX_DRUMS], mono(&drum));

    // The copy of a strip the tape records matches what you hear of it.
    {
        mixer_set_master_volume(1.0f);
        note(saw, 57, 0.8f);
        double heard = 0, copy = 0, other = 0;
        for (int b = 0; b < BLOCKS(0.5); b++) {
            const int16_t *blk = amy_simple_fill_buffer();
            const float *st = mixer_strip_block(1), *st0 = mixer_strip_block(0);
            for (int i = 0; i < AMY_BLOCK_SIZE; i++) {
                double h = (blk[2 * i] + blk[2 * i + 1]) / 2.0 / 32768.0;
                heard += h * h;
                copy += (double)st[i] * st[i];
                other += (double)st0[i] * st0[i];
            }
        }
        note(saw, 57, 0.0f);
        render(1.5);
        check("the tape's copy of a strip is as loud as you hear it", fabs(db(sqrt(copy), sqrt(heard))) < 0.3,
              "%+.2f dB, other strip %.1g", db(sqrt(copy), sqrt(heard)), other);
    }

    // Repatching keeps the bus.
    load(1, 6);
    render(0.05);
    Measure again = play(saw, 57, 0.6);
    check("a reloaded sound stays on its strip", again.peak[1] > 0 && again.peak[0] == 0, "peak %.3f / %.3f",
          again.peak[1], again.peak[0]);

    s_strip[1].level = 1.0f;
    apply(1, true);
    Measure loud = play(saw, 57, 0.6);
    check("fader up: louder by (1/0.75)^2", fabs(db(mono(&loud), mono(&clean)) - 40 * log10(1 / 0.75)) < 0.5,
          "%+.2f dB, want %+.2f", db(mono(&loud), mono(&clean)), 40 * log10(1 / 0.75));
    s_strip[1].level = MIX_LEVEL_UNITY;

    apply(1, false);
    Measure muted = play(saw, 57, 0.6);
    check("mute silences the strip", mono(&muted) < 1e-5, "rms %.6f", mono(&muted), 0);

    s_strip[1].knob[MIX_PAN] = 0.0f;
    apply(1, true);
    Measure left = play(saw, 57, 0.6);
    check("pan hard left: nothing on the right", left.rms[1] < 1e-5 && fabs(db(left.rms[0], clean.rms[0])) < 0.5,
          "R %.6f, L %+.2f dB", left.rms[1], db(left.rms[0], clean.rms[0]));
    s_strip[1].knob[MIX_PAN] = 0.5f;

    // DRIVE and CRUSH on a pure sine (part 1): any harmonics are theirs.
    wire("i1iv2uv0w0");
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "i1iV%.3f", mixer_synth_level(2.0f));  // about a supersaw note's level
        wire(buf);
    }
    const int sine = part_synth(0);
    Measure pure = play(sine, 57, 0.6);
    printf("(sine peaks at %.3f on its bus; a SUPERSAW note at %.3f)\n", pure.peak[0] * MIX_HEADROOM,
           clean.peak[1] * MIX_HEADROOM);
    double last_crest = pure.crest;
    for (int k = 1; k <= 4; k++) {
        s_strip[0].knob[MIX_DRIVE] = k * 0.25f;
        apply(0, true);
        Measure driven = play(sine, 57, 0.6);
        char what[80];
        snprintf(what, sizeof(what), "DRIVE %.2f squares the sine off more, level within 6 dB", k * 0.25);
        check(what, driven.crest < last_crest * 0.98 && fabs(db(mono(&driven), mono(&pure))) < 6,
              "crest x%.2f, %+.2f dB", driven.crest / pure.crest, db(mono(&driven), mono(&pure)));
        last_crest = driven.crest;
    }
    check("full DRIVE is properly squared off", last_crest < pure.crest * 0.8, "crest x%.2f",
          last_crest / pure.crest, 0);
    s_strip[0].knob[MIX_DRIVE] = 0.03f;
    apply(0, true);
    Measure gentle = play(sine, 57, 0.6);
    check("a touch of DRIVE is a gentle knee", gentle.crest > pure.crest * 0.85, "crest x%.3f",
          gentle.crest / pure.crest, 0);
    s_strip[0].knob[MIX_DRIVE] = 0.0f;
    for (int k = 1; k <= 4; k++) {
        s_strip[0].knob[MIX_CRUSH] = k * 0.25f;
        apply(0, true);
        Measure crushed = play(sine, 57, 0.6);
        char what[80];
        snprintf(what, sizeof(what), "CRUSH %.2f adds steps, level within 6 dB", k * 0.25);
        check(what, crushed.bright > pure.bright * 1.1 && fabs(db(mono(&crushed), mono(&pure))) < 6,
              "bright x%.2f, %+.2f dB", crushed.bright / pure.bright, db(mono(&crushed), mono(&pure)));
    }
    s_strip[0].knob[MIX_CRUSH] = 0.0f;
    apply(0, true);
    // A loud chord through CRUSH at its lightest: no clipping at the crusher's rails.
    Measure chord[2];
    for (int k = 0; k < 2; k++) {
        s_strip[1].knob[MIX_CRUSH] = k ? 0.03f : 0.0f;
        apply(1, true);
        render(0.1);
        note(saw, 45, 1.0f);
        note(saw, 52, 1.0f);
        note(saw, 57, 1.0f);
        chord[k] = render(0.5);
        note(saw, 45, 0.0f);
        note(saw, 52, 0.0f);
        note(saw, 57, 0.0f);
        render(1.5);
    }
    check("a loud chord through light CRUSH keeps its level", fabs(db(mono(&chord[1]), mono(&chord[0]))) < 0.5,
          "%+.2f dB, peak %.2f", db(mono(&chord[1]), mono(&chord[0])), chord[0].peak[1] * MIX_HEADROOM);
    s_strip[1].knob[MIX_CRUSH] = 0.0f;
    apply(1, true);
    // DRIVE on the SUPERSAW itself: the level stays usable all the way up.
    for (int k = 1; k <= 4; k++) {
        s_strip[1].knob[MIX_DRIVE] = k * 0.25f;
        apply(1, true);
        Measure d = play(saw, 57, 0.6);
        char what[80];
        snprintf(what, sizeof(what), "SUPERSAW with DRIVE %.2f within 6 dB of clean", k * 0.25);
        check(what, fabs(db(mono(&d), mono(&clean))) < 6, "%+.2f dB", db(mono(&d), mono(&clean)), 0);
    }
    s_strip[1].knob[MIX_DRIVE] = 0.0f;
    apply(1, true);

    s_strip[1].knob[MIX_TONE] = 0.0f;
    apply(1, true);
    Measure dark = play(saw, 57, 0.6);
    s_strip[1].knob[MIX_TONE] = 1.0f;
    apply(1, true);
    Measure bright = play(saw, 57, 0.6);
    check("TONE: dark < flat < bright", dark.bright < clean.bright && bright.bright > clean.bright,
          "%.3f < %.3f < ...", dark.bright, clean.bright);

    // Effects on part 2's strip leave part 3 bit-for-bit alone.
    s_strip[1].knob[MIX_TONE] = 0.5f;
    apply(1, true);
    const int bell = part_synth(2);
    Measure bell_clean = play(bell, 69, 0.5);
    s_strip[1].knob[MIX_DRIVE] = 1.0f;
    s_strip[1].knob[MIX_CRUSH] = 1.0f;
    apply(1, true);
    Measure bell_beside = play(bell, 69, 0.5);
    check("another strip's FX don't touch this one", mono(&bell_beside) == mono(&bell_clean),
          "rms %.6f vs %.6f", mono(&bell_beside), mono(&bell_clean));
    s_strip[1].knob[MIX_DRIVE] = s_strip[1].knob[MIX_CRUSH] = 0.0f;
    apply(1, true);

    // The master echo reaches the parts: a longer tail after note-off.
    note(bell, 69, 0.8f);
    render(0.3);
    note(bell, 69, 0.0f);
    render(0.9);
    Measure dry_tail = render(0.6);
    mix_echo(0.8f);
    render(0.1);
    note(bell, 69, 0.8f);
    render(0.3);
    note(bell, 69, 0.0f);
    render(0.9);
    Measure wet_tail = render(0.6);
    check("master echo still reaches the parts", mono(&wet_tail) > mono(&dry_tail) * 2 + 1e-4, "tail %.5f vs %.5f",
          mono(&wet_tail), mono(&dry_tail));

    // A strip's ECHO send at 0: no echo from it, though the echo is on.
    s_strip[2].knob[MIX_ECHO] = 0.0f;
    apply(2, true);
    render(1.0);
    note(bell, 69, 0.8f);
    render(0.3);
    note(bell, 69, 0.0f);
    render(0.9);
    Measure dry_send = render(0.6);
    check("ECHO send at 0: that strip stays dry", mono(&dry_send) < mono(&wet_tail) * 0.1, "tail %.5f vs %.5f",
          mono(&dry_send), mono(&wet_tail));
    s_strip[2].knob[MIX_ECHO] = 1.0f;
    apply(2, true);
    // The returns hold only the effect: with the echo and reverb levels at 0
    // the mix is exactly the dry strips (the sends are taken back out).
    mix_echo(0.0f);
    mix_verb(0.0f);
    Measure sent[2];
    for (int k = 0; k < 2; k++) {
        s_strip[1].knob[MIX_ECHO] = s_strip[1].knob[MIX_VERB] = k ? 1.0f : 0.0f;
        apply(1, true);
        render(1.5);
        note(saw, 57, 0.8f);
        sent[k] = render(0.5);
        note(saw, 57, 0.0f);
    }
    render(1.5);
    check("full sends with the effects off change nothing", fabs(db(mono(&sent[1]), mono(&sent[0]))) < 0.05, "%+.3f dB",
          db(mono(&sent[1]), mono(&sent[0])), 0);

    // The echo and reverb fall asleep when nothing feeds them (mixer.h), and
    // wake with the first note: the tail is the same as if they never slept.
    mix_echo(0.3f);
    mix_verb(0.3f);
    Measure tail[2];
    for (int k = 0; k < 2; k++) {
        if (k) {
            mixer_set_fx_level(0, 0.3f);
            mixer_set_fx_level(1, 0.3f);
        }
        render(6.0);
        if (k)
            check("silence: the echo and the reverb fall asleep", mixer_fx_asleep(0) && mixer_fx_asleep(1), "echo %.0f reverb %.0f",
                  mixer_fx_asleep(0), mixer_fx_asleep(1));
        note(saw, 57, 0.8f);
        render(0.3);
        if (k)
            check("a note wakes them", !mixer_fx_asleep(0) && !mixer_fx_asleep(1), "echo %.0f reverb %.0f", mixer_fx_asleep(0),
                  mixer_fx_asleep(1));
        note(saw, 57, 0.0f);
        render(0.3);
        tail[k] = render(0.6);
    }
    check("...and the tail is what it was without the nap", mono(&tail[0]) > 1e-4 && fabs(db(mono(&tail[1]), mono(&tail[0]))) < 0.2,
          "%.5f, %+.3f dB", mono(&tail[0]), db(mono(&tail[1]), mono(&tail[0])));
    // A strip's TONE is a filter that never rings down to exactly nothing:
    // that mustn't keep the strip, and so the effects, awake for good.
    s_strip[1].knob[MIX_TONE] = 0.85f;
    apply(1, true);
    note(saw, 57, 0.8f);
    render(0.3);
    note(saw, 57, 0.0f);
    render(8.0);
    check("...also after a strip with its TONE turned", mixer_fx_asleep(0) && mixer_fx_asleep(1), "echo %.0f reverb %.0f",
          mixer_fx_asleep(0), mixer_fx_asleep(1));
    s_strip[1].knob[MIX_TONE] = 0.5f;
    apply(1, true);
    // Sends that differ (the reverb's bus is the echo's plus the difference).
    Measure uneven[3];
    for (int k = 0; k < 3; k++) {
        s_strip[1].knob[MIX_ECHO] = k == 1 ? 0.0f : 1.0f;
        s_strip[1].knob[MIX_VERB] = k == 2 ? 0.0f : 1.0f;
        apply(1, true);
        render(4.0);
        note(saw, 57, 0.8f);
        render(0.3);
        note(saw, 57, 0.0f);
        render(0.3);
        uneven[k] = render(0.6);
    }
    check("the echo's send off: a smaller tail, not none", mono(&uneven[1]) > 1e-5 && mono(&uneven[1]) < mono(&uneven[0]) * 0.95,
          "%.5f vs %.5f", mono(&uneven[1]), mono(&uneven[0]));
    check("the reverb's send off: a smaller tail, not none", mono(&uneven[2]) > 1e-5 && mono(&uneven[2]) < mono(&uneven[0]) * 0.95,
          "%.5f vs %.5f", mono(&uneven[2]), mono(&uneven[0]));
    s_strip[1].knob[MIX_ECHO] = s_strip[1].knob[MIX_VERB] = 1.0f;
    apply(1, true);
    mixer_set_fx_level(0, -1.0f);
    mixer_set_fx_level(1, -1.0f);
    mix_echo(0.0f);
    mix_verb(0.0f);
    render(1.0);

    // How high the meters go for a loud part, so the UI can scale them.
    s_strip[1].level = 1.0f;
    apply(1, true);
    note(saw, 45, 1.0f);
    note(saw, 52, 1.0f);
    note(saw, 57, 1.0f);
    Measure full = render(0.6);
    printf("meter: a loud 3-note chord peaks at %.3f (bus units)\n", full.peak[1]);

    printf(s_fail ? "\n%d mixer checks FAILED\n" : "\nall mixer checks OK\n", s_fail);
    return s_fail ? 1 : 0;
}
