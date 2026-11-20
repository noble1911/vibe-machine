// bench_probe -- what costs CPU: AMY rendering the app's setup (mixer buses,
// master echo + reverb) for a few scenes. Host timings, so only the
// ratios mean anything for the 3DS.   make -C tests build/bench_probe
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "amy.h"
#include "mixer.h"
#include "sampler.h"
#include "sounds.h"

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static void wire(const char *m) {
    char b[512];
    snprintf(b, sizeof(b), "%s", m);
    amy_add_message(b);
}
static void load(int part, int sound, int patch) {
    const Sound *s = sound_get(sound);
    char b[512];
    if (s->kind == SOUND_BANK) {
        amy_event e = amy_default_event();
        e.synth = (uint8_t)part_synth(part);
        e.num_voices = (uint16_t)s->voices;
        e.patch_number = (uint16_t)patch;
        amy_add_event(&e);
    } else {
        snprintf(b, sizeof(b), "i%div%du%s", part_synth(part), s->voices, s->patch);
        wire(b);
    }
    snprintf(b, sizeof(b), "i%diV%.3f", part_synth(part), mixer_synth_level(s->level));
    wire(b);
    amy_event e = amy_default_event();
    e.synth = (uint8_t)part_synth(part);
    e.bus = (uint16_t)part;
    amy_add_event(&e);
}
static void note(int synth, int n, float v) {
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    e.midi_note = (float)n;
    e.velocity = v;
    amy_add_event(&e);
}
static double cost(const char *what, int blocks) {
    for (int i = 0; i < 20; i++) amy_simple_fill_buffer();  // settle
    double t0 = now_us();
    for (int i = 0; i < blocks; i++) amy_simple_fill_buffer();
    double us = (now_us() - t0) / blocks;
    int audible = 0;
    for (int o = 0; o < AMY_OSCS; o++) audible += synth[o] && synth[o]->status == SYNTH_AUDIBLE;
    printf("%-44s %7.1f us/block  (%d oscs sounding)\n", what, us, audible);
    return us;
}
static void silence(void) {
    for (int k = 0; k < PARTS; k++) {
        amy_event e = amy_default_event();
        e.synth = (uint8_t)part_synth(k);
        e.velocity = 0;
        amy_add_event(&e);
    }
    for (int i = 0; i < 2000; i++) amy_simple_fill_buffer();
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
    amy_event m = amy_default_event();
    m.bus = MIX_MASTER_BUS;
    m.volume = 1.0f;
    m.echo_level = 0.15f;
    m.echo_delay_ms = 375;
    m.echo_max_delay_ms = 750;
    m.echo_feedback = 0.45f;
    m.reverb_level = 0.25f;
    amy_add_event(&m);
    static MixStrip st[MIX_STRIPS];
    for (int k = 0; k < MIX_STRIPS; k++) {
        mixer_strip_default(&st[k]);
        mixer_set_gain(k, &st[k], true);
    }
    load(0, 0, 0);  // JUNO
    load(1, 2, 0);  // PLUCK
    load(2, 4, 0);  // BELL
    static int16_t boing[SAMPLER_RATE];
    int n = sampler_boing(boing, SAMPLER_RATE);
    memcpy(pcm_load(SAMPLER_PRESET, n, SAMPLER_RATE, 1, 60, 0, 0), boing, n * 2);

    const int B = 30000;
    double base = cost("idle (echo + reverb on master)", B);
    note(SYNTH_DRUMS, 36, 1);
    note(SYNTH_DRUMS, 42, 1);
    cost("kick + hat ringing", B);
    silence();
    note(1, 60, 0.8f);
    double one = cost("JUNO patch 0: one note", B);
    note(1, 64, 0.8f);
    note(1, 67, 0.8f);
    double chord = cost("JUNO patch 0: 3-note chord", B);
    note(1, 72, 0.8f);
    note(1, 76, 0.8f);
    note(1, 79, 0.8f);
    cost("JUNO patch 0: 6 notes (chord + release tails)", B);
    silence();
    load(0, 0, 10);
    note(1, 60, 0.8f); note(1, 64, 0.8f); note(1, 67, 0.8f);
    cost("JUNO patch 10: 3-note chord", B);
    silence();
    load(0, 1, 128);
    note(1, 60, 0.8f); note(1, 64, 0.8f); note(1, 67, 0.8f);
    cost("DX7 patch 128: 3-note chord", B);
    silence();
    load(0, 6, 0);
    note(1, 60, 0.8f); note(1, 64, 0.8f); note(1, 67, 0.8f);
    cost("SUPERSAW: 3-note chord", B);
    silence();
    note(2, 60, 0.8f); note(2, 64, 0.8f); note(2, 67, 0.8f);
    cost("PLUCK: 3-note chord", B);
    silence();
    note(3, 72, 0.8f); note(3, 76, 0.8f); note(3, 79, 0.8f);
    cost("BELL: 3-note chord", B);
    silence();
    m = amy_default_event();
    m.bus = MIX_MASTER_BUS;
    m.reverb_level = 0.0f;
    m.echo_level = 0.0f;
    amy_add_event(&m);
    double dry = cost("idle, echo + reverb off", B);
    m = amy_default_event();
    m.bus = MIX_MASTER_BUS;
    m.echo_level = 0.15f;
    amy_add_event(&m);
    cost("idle, echo only", B);
    m = amy_default_event();
    m.bus = MIX_MASTER_BUS;
    m.echo_level = 0.0f;
    m.reverb_level = 0.25f;
    amy_add_event(&m);
    cost("idle, reverb only", B);
    m = amy_default_event();
    m.bus = MIX_MASTER_BUS;
    m.reverb_level = 0.0f;
    amy_add_event(&m);
    load(0, 0, 0);
    note(1, 60, 0.8f); note(1, 64, 0.8f); note(1, 67, 0.8f);
    note(1, 72, 0.8f); note(1, 76, 0.8f); note(1, 79, 0.8f);
    cost("JUNO 6 notes, no echo/reverb", B);
    silence();
    printf("\nmaster echo + reverb cost about %.0f%% of an idle block; a JUNO chord is %.1fx idle (one note %.1fx)\n",
           100 * (base - dry) / base, chord / base, one / base);
    return 0;
}
