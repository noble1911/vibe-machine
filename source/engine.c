// engine.c -- see engine.h.

#include "engine.h"

#include <3ds.h>

#include "amy.h"

static LightLock s_lock;

static const char *const s_param_names[PARAM_COUNT] = {
    "TONE", "ECHO", "VERB", "VOL",
};

static void send_locked(amy_event *e) {
    LightLock_Lock(&s_lock);
    amy_add_event(e);
    LightLock_Unlock(&s_lock);
}

void engine_init(void) {
    LightLock_Init(&s_lock);

    amy_config_t c = amy_default_config();
    // Clamshell drives AMY by hand: no AMY-owned audio device, MIDI port,
    // threads or second core. default_synths gives us the GM drum kit on
    // synth 10 for free.
    c.audio = AMY_AUDIO_IS_NONE;
    c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0;
    c.platform.multithread = 0;
    c.features.default_synths = 1;
    c.features.startup_bleep = 0;
    c.features.audio_in = 0;
    amy_start(c);

    // Echo character is fixed for now; the ECHO slider only sets the level.
    amy_event e = amy_default_event();
    e.echo_level = 0.0f;
    e.echo_delay_ms = 375.0f;
    e.echo_max_delay_ms = 750.0f;
    e.echo_feedback = 0.45f;
    send_locked(&e);

    engine_set_patch(0);
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

void engine_set_patch(int patch) {
    // Loading a patch rebuilds the synth's voices, which also silences any
    // notes still sounding on it.
    amy_event e = amy_default_event();
    e.synth = SYNTH_LEAD;
    e.num_voices = LEAD_VOICES;
    e.patch_number = (uint16_t)patch;
    send_locked(&e);
}

void engine_set_param(EngineParam param, float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    amy_event e = amy_default_event();
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
