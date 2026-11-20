// engine.c -- see engine.h.

#include "engine.h"

#include <3ds.h>

#include "amy.h"
#include "mixer.h"
#include "sampler.h"
#include "sounds.h"

#include <stdio.h>
#include <string.h>

static LightLock s_lock;

static const char *const s_param_names[PARAM_COUNT] = {
    "TONE", "ECHO", "VERB", "VOL",
};

static void wire_locked(const char *msg);

static void send_locked(amy_event *e) {
    LightLock_Lock(&s_lock);
    amy_add_event(e);
    LightLock_Unlock(&s_lock);
}

void engine_init(void) {
    LightLock_Init(&s_lock);

    amy_config_t c = amy_default_config();
    // Vibe Machine drives AMY by hand: no AMY-owned audio device, MIDI port,
    // threads or second core. default_synths gives us the GM drum kit on
    // synth 10 for free.
    c.audio = AMY_AUDIO_IS_NONE;
    c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0;
    c.platform.multithread = 0;
    c.features.default_synths = 1;
    c.features.startup_bleep = 0;
    c.features.audio_in = 0;
    // One bus per MIX strip plus the master; mixer_hook sums the strips into
    // the master (see mixer.h).
    c.max_buses = MIX_BUSES;
    c.amy_external_bus_postprocess_hook = mixer_hook;
    amy_start(c);

    // The drum kit plays through its own strip, with the same headroom as
    // the parts (engine_load_sound).
    amy_event d = amy_default_event();
    d.synth = SYNTH_DRUMS;
    d.bus = MIX_DRUMS;
    send_locked(&d);
    char lvl[32];
    snprintf(lvl, sizeof(lvl), "i%diV%.3f", SYNTH_DRUMS, mixer_synth_level(1.0f));
    wire_locked(lvl);

    // The master bus. Echo character is fixed for now; the ECHO slider only
    // sets the level.
    amy_event e = amy_default_event();
    e.bus = MIX_MASTER_BUS;
    e.volume = 1.0f;
    e.echo_level = 0.0f;
    e.echo_delay_ms = 375.0f;
    e.echo_max_delay_ms = 750.0f;
    e.echo_feedback = 0.45f;
    send_locked(&e);

    // Each part's sound is loaded by main.c (engine_load_sound) at startup.

    // The MIC sound's placeholder until something is recorded.
    static int16_t boing[SAMPLER_RATE];
    int n = sampler_boing(boing, SAMPLER_RATE);
    engine_load_sample(boing, n, SAMPLER_RATE);
}

void engine_exit(void) {
    LightLock_Lock(&s_lock);
    amy_stop();
    LightLock_Unlock(&s_lock);
}

void engine_note_on(int synth, int note, float velocity) {
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    e.midi_note = (float)note;
    e.velocity = velocity;
    send_locked(&e);
}

void engine_note_off(int synth, int note) {
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    e.midi_note = (float)note;
    e.velocity = 0.0f;
    send_locked(&e);
}


static void wire_locked(const char *msg) {
    char buf[512];  // amy_add_message wants a mutable string
    snprintf(buf, sizeof(buf), "%s", msg);
    LightLock_Lock(&s_lock);
    amy_add_message(buf);
    LightLock_Unlock(&s_lock);
}

void engine_load_sound(int part, int sound, int patch) {
    const Sound *s = sound_get(sound);
    const int synth = part_synth(part);
    char msg[512];
    if (s->kind == SOUND_BANK) {
        amy_event e = amy_default_event();
        e.synth = (uint8_t)synth;
        e.num_voices = (uint16_t)s->voices;
        e.patch_number = (uint16_t)patch;
        send_locked(&e);
    } else {
        snprintf(msg, sizeof(msg), "i%div%du%s", synth, s->voices, s->patch);
        wire_locked(msg);
    }
    snprintf(msg, sizeof(msg), "i%diV%.3f", synth, mixer_synth_level(s->level));
    wire_locked(msg);
    // Onto the part's MIX strip. (AMY keeps a synth's bus across reloads;
    // saying it again costs nothing.)
    amy_event b = amy_default_event();
    b.synth = (uint8_t)synth;
    b.bus = (uint16_t)part;
    send_locked(&b);
}

void engine_set_strip(int strip, const struct MixStrip *s, bool audible) {
    if (strip < 0 || strip >= MIX_STRIPS) return;
    amy_event e = amy_default_event();
    mixer_fill_event(strip, s, &e);
    send_locked(&e);
    mixer_set_gain(strip, s, audible);
}

bool engine_load_sample(const int16_t *data, int frames, int rate) {
    if (frames <= 0) return false;
    for (int part = 0; part < PARTS; part++) {  // any part may be playing the MIC take
        amy_event off = amy_default_event();
        off.synth = (uint8_t)part_synth(part);
        off.velocity = 0.0f;  // note-off for every voice of the synth
        send_locked(&off);
    }
    LightLock_Lock(&s_lock);
    // midinote 60: the C key plays the take at its recorded pitch.
    int16_t *ram = pcm_load(SAMPLER_PRESET, (uint32_t)frames, (uint32_t)rate, 1, 60.0f, 0, 0);
    if (ram) memcpy(ram, data, (size_t)frames * sizeof(int16_t));
    LightLock_Unlock(&s_lock);
    if (ram) sound_set_sample_frames(frames);
    return ram != NULL;
}

void engine_set_macro(int part, int sound, int m, float value) {
    char msg[128];
    if (sound_macro_wire(sound, m, value, part_synth(part), msg, sizeof(msg))) wire_locked(msg);
}

void engine_set_param(EngineParam param, float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    amy_event e = amy_default_event();
    e.bus = MIX_MASTER_BUS;
    switch (param) {
        case PARAM_TONE:
            // Tilt EQ around the middle: 0 = dark, 0.5 = flat, 1 = bright (dB).
            e.eq_l = (0.5f - v) * 24.0f;
            e.eq_h = (v - 0.5f) * 24.0f;
            break;
        case PARAM_ECHO:
            e.echo_level = v;
            break;
        case PARAM_REVERB:
            e.reverb_level = v;
            break;
        case PARAM_VOLUME:
            e.volume = v * 2.5f;
            break;
        default:
            return;
    }
    send_locked(&e);
}

const char *engine_param_name(EngineParam param) {
    return (param >= 0 && param < PARAM_COUNT) ? s_param_names[param] : "?";
}

const int16_t *engine_render_block(void) {
    LightLock_Lock(&s_lock);
    const int16_t *block = amy_simple_fill_buffer();
    LightLock_Unlock(&s_lock);
    return block;
}

int engine_block_frames(void) { return AMY_BLOCK_SIZE; }

int engine_sample_rate(void) { return AMY_SAMPLE_RATE; }
