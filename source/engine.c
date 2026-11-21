// engine.c -- see engine.h.

#include "engine.h"

#include <3ds.h>

#include "amy.h"
#include "amy_split.h"
#include "mixer.h"
#include "sampler.h"
#include "drums.h"
#include "sounds.h"
#include "tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LightLock s_lock;
static void load_drum_banks(void);

// The second core (the old 3DS's "system core", which an app may use up to
// a time limit): it renders part of every block's oscillators while the
// audio thread renders the rest, then the audio thread mixes and runs the
// effects. See amy_split.h.
#define CORE1_SHARE 0.6f  // core 0 also runs the effects, so core 1 takes more voices
static Thread s_worker;
static LightEvent s_go, s_done;
static volatile uint16_t s_split;
static volatile bool s_worker_quit;

static void render_worker(void *arg) {
    (void)arg;
    for (;;) {
        LightEvent_Wait(&s_go);
        if (s_worker_quit) return;
        amy_render(s_split, AMY_OSCS, 1);
        LightEvent_Signal(&s_done);
    }
}

// MIC: which parts play it, and how many slices it's chopped into. Read by
// note on/off from both threads (the sequencer calls in from the audio one).
static volatile bool s_part_mic[PARTS];
static volatile int s_slices;

// A note on a part playing MIC always names the sample preset, so voices
// never keep a slice's preset after CHOP is turned off. With CHOP on, the
// key picks the slice and the slice plays at its own pitch.
static void mic_note(amy_event *e, int synth, int note) {
    e->midi_note = (float)note;
    const int part = synth - part_synth(0);
    if (part < 0 || part >= PARTS || !s_part_mic[part]) return;
    const int slices = s_slices;
    if (slices > 0) {
        e->preset = (uint16_t)(SAMPLER_PRESET + 1 + sampler_slice_of_pitch(note, slices));
        e->midi_note = SAMPLER_SLICE_NOTE;
    } else {
        e->preset = SAMPLER_PRESET;
    }
}

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
    // threads or second core, and no default synths (the parts and the drum
    // lanes are all set up here).
    c.audio = AMY_AUDIO_IS_NONE;
    c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0;
    c.platform.multithread = 0;
    c.features.default_synths = 0;
    c.features.startup_bleep = 0;
    c.features.audio_in = 0;
    // One bus per MIX strip plus the master; mixer_hook sums the strips into
    // the master (see mixer.h).
    c.max_buses = MIX_BUSES;
    c.amy_external_bus_postprocess_hook = mixer_hook;
    amy_start(c);

    // A worker on core 1 (if the system lets us have it: otherwise everything
    // renders on this core, as before).
    LightEvent_Init(&s_go, RESET_ONESHOT);
    LightEvent_Init(&s_done, RESET_ONESHOT);
    APT_SetAppCpuTimeLimit(80);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_worker = threadCreate(render_worker, NULL, 64 * 1024, prio - 2 < 0x18 ? 0x18 : prio - 2, 1, false);

    // The drum kits' samples (all but the TR-808, which is built in): a
    // 3.7 MB file in the app's romfs. Without it only the 808 plays.
    load_drum_banks();
    // The drums: a little sample player per lane (two voices, so a hit can
    // ring under the next), on the DRUMS strip, with the same headroom as
    // the parts. A lane per synth, so lanes never steal each other's voices.
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        const int syn = DRUM_SYNTH0 + lane;
        char m[64];
        snprintf(m, sizeof(m), "i%div2uv0w7", syn);
        wire_locked(m);
        amy_event d = amy_default_event();
        d.synth = (uint8_t)syn;
        d.bus = MIX_DRUMS;
        send_locked(&d);
        snprintf(m, sizeof(m), "i%diV%.3f", syn, mixer_synth_level(1.0f));
        wire_locked(m);
        snprintf(m, sizeof(m), "i%dv0f,,,,,,0", syn);  // pitch bend leaves the drums alone
        wire_locked(m);
    }
    DrumKit kit;
    drum_kit_stock(&kit, 0);
    engine_set_kit(&kit);

    // ORGAN's and VOX's wavetables, built once (tables.c).
    {
        static int16_t organ[ORGAN_CYCLES * TABLE_CYCLE], vox[VOX_CYCLES * TABLE_CYCLE];
        tables_organ(organ);
        tables_vox(vox);
        LightLock_Lock(&s_lock);
        int16_t *ram = pcm_load(ORGAN_PRESET, ORGAN_CYCLES * TABLE_CYCLE, 44100, 1, 60.0f, 0, 0);
        if (ram) memcpy(ram, organ, sizeof(organ));
        ram = pcm_load(VOX_PRESET, VOX_CYCLES * TABLE_CYCLE, 44100, 1, 60.0f, 0, 0);
        if (ram) memcpy(ram, vox, sizeof(vox));
        LightLock_Unlock(&s_lock);
    }

    // The master bus (dry), the echo bus and the reverb bus (see mixer.h),
    // all at full volume to start with. Echo character is fixed for now;
    // the ECHO slider only sets the level.
    mixer_set_master_volume(1.0f);
    for (int bus = MIX_MASTER_BUS; bus <= MIX_VERB_BUS; bus++) {
        amy_event v = amy_default_event();
        v.bus = (uint16_t)bus;
        v.volume = 1.0f;
        send_locked(&v);
    }
    amy_event e = amy_default_event();
    e.bus = MIX_ECHO_BUS;
    e.echo_level = 0.0f;
    e.echo_delay_ms = 375.0f;
    e.echo_max_delay_ms = 750.0f;
    e.echo_feedback = 0.45f;
    send_locked(&e);

    // Each part's sound is loaded by main.c (engine_load_sound) at startup.

    // The MIC sound's placeholder until something is recorded.
    static int16_t boing[SAMPLER_RATE];
    int n = sampler_boing(boing, SAMPLER_RATE);
    engine_load_sample(boing, n, SAMPLER_RATE, NULL);
}

void engine_exit(void) {
    if (s_worker) {
        s_worker_quit = true;
        LightEvent_Signal(&s_go);
        threadJoin(s_worker, U64_MAX);
        threadFree(s_worker);
        s_worker = NULL;
    }
    LightLock_Lock(&s_lock);
    amy_stop();
    LightLock_Unlock(&s_lock);
}

// The kit as the engine plays it: each lane's note and level (read by
// note-ons from both threads; written under the lock).
static float s_lane_note[DRUM_LANES], s_lane_gain[DRUM_LANES];

static void drum_hit(int note, float velocity) {
    const int lane = drum_lane_of_note(note);
    if (lane < 0) return;
    LightLock_Lock(&s_lock);
    if (lane == DRUM_LANE_HAT) {  // the closed hat chokes the open one
        amy_event c = amy_default_event();
        c.synth = (uint8_t)(DRUM_SYNTH0 + DRUM_LANE_OPEN);
        c.velocity = 0.0f;  // no note: all of that lane's notes off
        amy_add_event(&c);
    }
    amy_event e = amy_default_event();
    e.synth = (uint8_t)(DRUM_SYNTH0 + lane);
    e.midi_note = s_lane_note[lane];
    e.velocity = velocity * s_lane_gain[lane];
    amy_add_event(&e);
    LightLock_Unlock(&s_lock);
}

void engine_set_kit(const DrumKit *k) {
    char m[48];
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        snprintf(m, sizeof(m), "i%dv0p%d", DRUM_SYNTH0 + lane, k->lane[lane].preset);
        wire_locked(m);
    }
    LightLock_Lock(&s_lock);
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        s_lane_note[lane] = drum_lane_note(k, lane);
        s_lane_gain[lane] = drum_lane_gain(k, lane);
    }
    LightLock_Unlock(&s_lock);
}

bool engine_load_part_sample(int part, const int16_t *data, int frames, int rate) {
    if (part < 0 || part >= PARTS || frames <= 0) return false;
    LightLock_Lock(&s_lock);
    int16_t *ram = pcm_load((uint16_t)(PART_SAMPLE_PRESET + part), (uint32_t)frames, (uint32_t)rate, 1, 60.0f, 0, 0);
    if (ram) memcpy(ram, data, (size_t)frames * sizeof(int16_t));
    LightLock_Unlock(&s_lock);
    return ram != NULL;
}

bool engine_load_drum_sample(int lane, const int16_t *data, int frames, int rate) {
    if (lane < 0 || lane >= DRUM_LANES || frames <= 0) return false;
    LightLock_Lock(&s_lock);
    int16_t *ram = pcm_load((uint16_t)(DRUM_USER_PRESET + lane), (uint32_t)frames, (uint32_t)rate, 1, 60.0f, 0, 0);
    if (ram) memcpy(ram, data, (size_t)frames * sizeof(int16_t));
    LightLock_Unlock(&s_lock);
    return ram != NULL;
}

// AMY's Gamma9001 drum banks, read whole from romfs into memory it keeps
// pointing at for good.
static void load_drum_banks(void) {
    FILE *f = fopen("romfs:/drums.bin", "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    int16_t *data = n > 0 ? malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) == (size_t)n)
        amy_set_gamma9001_pcm(data);
    else
        free(data);
    fclose(f);
}

void engine_note_on(int synth, int note, float velocity) {
    if (synth == SYNTH_DRUMS) {
        drum_hit(note, velocity);
        return;
    }
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    mic_note(&e, synth, note);
    e.velocity = velocity;
    send_locked(&e);
}

void engine_note_off(int synth, int note) {
    if (synth == SYNTH_DRUMS) return;  // drums are one-shots: they play to their end
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    mic_note(&e, synth, note);
    e.velocity = 0.0f;
    send_locked(&e);
}


static void wire_locked(const char *msg) {
    char buf[1024];  // amy_add_message wants a mutable string
    snprintf(buf, sizeof(buf), "%s", msg);
    LightLock_Lock(&s_lock);
    amy_add_message(buf);
    LightLock_Unlock(&s_lock);
}

static int s_part_sound[PARTS], s_part_patch[PARTS];
static uint32_t s_part_key[PARTS];

// DX7 BRIGHT: each operator that modulates another (by the patch's
// algorithm) has its level scaled. AMY's writes are absolute, so the
// patch's own levels are read once the patch has loaded (on the audio
// thread, after a render: see dx7_bright_tick).
struct FmAlgorithm { uint8_t ops[6]; };  // as in AMY's algorithms.c
extern const struct FmAlgorithm algorithms[33];
extern uint16_t *voice_to_base_osc;
static volatile float s_bright[PARTS] = {1.0f, 1.0f, 1.0f};
static volatile bool s_bright_due[PARTS];  // apply s_bright after the next render
static float s_dx7_level[PARTS][6];
static bool s_dx7_known[PARTS];             // s_dx7_level holds this patch's levels

static void dx7_bright_tick(void) {  // audio thread, AMY lock held
    for (int part = 0; part < PARTS; part++) {
        if (!s_bright_due[part]) continue;
        s_bright_due[part] = false;
        uint16_t voices[16];
        const int syn = part_synth(part);
        if (instrument_get_num_voices(syn, voices) < 1 || !voice_to_base_osc) continue;
        const uint16_t base = voice_to_base_osc[voices[0]];
        if (base + 7 >= AMY_OSCS || !synth[base] || !synth[base + 2]) continue;
        const int algo = synth[base]->algorithm;
        if (algo < 0 || algo > 32) continue;
        if (!s_dx7_known[part]) {
            for (int i = 0; i < 6; i++) s_dx7_level[part][i] = synth[base + 2 + i] ? synth[base + 2 + i]->amp_coefs[0] : 0.0f;
            s_dx7_known[part] = true;
        }
        float k = s_bright[part];
        if (k < 0.01f) k = 0.01f;  // a modulator at exactly 0 turns the output to garbage
        for (int i = 0; i < 6; i++) {
            if (!(algorithms[algo].ops[i] & 3)) continue;  // a carrier
            char msg[48];
            snprintf(msg, sizeof(msg), "i%dv%da%.4f", syn, 2 + i, (double)(s_dx7_level[part][i] * k));
            amy_add_message(msg);
        }
    }
}

static void send_edit(int part, int sound, const SynthEdit *e) {
    char cmds[16][64];
    const int n = sound_edit_wire(sound, e, part_synth(part), &cmds[0][0], 64, 16);
    for (int i = 0; i < n; i++) wire_locked(cmds[i]);
}

void engine_load_sound(int part, int sound, int patch, const SynthEdit *edit) {
    const Sound *s = sound_get(sound);
    SynthEdit none;
    if (!edit) {
        memset(&none, 0, sizeof(none));
        none.unison = 1;
        edit = &none;
    }
    if (part >= 0 && part < PARTS) {
        s_part_mic[part] = sound == sound_mic_index();
        s_part_sound[part] = sound;
        s_part_patch[part] = patch;
        s_part_key[part] = sound_edit_key(sound, edit);
        s_dx7_known[part] = false;  // a new patch: its own levels are read again
        s_bright[part] = sound_edit_bright(sound, edit);
        s_bright_due[part] = sound == sound_dx7_index() && s_bright[part] != 1.0f;
    }
    const int synth = part_synth(part);
    char msg[512];
    if (s->kind == SOUND_BANK) {
        amy_event e = amy_default_event();
        e.synth = (uint8_t)synth;
        e.num_voices = (uint16_t)sound_edit_voices(sound, edit);
        e.patch_number = (uint16_t)patch;
        send_locked(&e);
    } else {
        char body[400];
        sound_edit_patch(sound, edit, body, sizeof(body));
        snprintf(msg, sizeof(msg), "i%div%du%s", synth, sound_edit_voices(sound, edit), body);
        wire_locked(msg);
        // Each part draws its own wave, and plays its own SAMPLE.
        if (sound == sound_draw_index()) {
            snprintf(msg, sizeof(msg), "i%dv0p%d", synth, DRAW_PRESET + part);
            wire_locked(msg);
        }
        if (sound == sound_sample_index()) {
            snprintf(msg, sizeof(msg), "i%dv0p%d", synth, PART_SAMPLE_PRESET + part);
            wire_locked(msg);
        }
    }
    send_edit(part, sound, edit);
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

static bool load_preset(int preset, const int16_t *data, int frames, int rate) {
    // midinote 60: the C key plays the sample at its recorded pitch.
    int16_t *ram = pcm_load((uint16_t)preset, (uint32_t)frames, (uint32_t)rate, 1, 60.0f, 0, 0);
    if (ram) memcpy(ram, data, (size_t)frames * sizeof(int16_t));
    return ram != NULL;
}

bool engine_load_sample(const int16_t *data, int frames, int rate, const SampleEdit *edit) {
    if (frames <= 0) return false;
    SampleEdit ed;
    if (edit)
        ed = *edit;
    else
        sampler_edit_default(&ed);
    sampler_edit_clamp(&ed);
    for (int part = 0; part < PARTS; part++) {  // any part may be playing the MIC take
        amy_event off = amy_default_event();
        off.synth = (uint8_t)part_synth(part);
        off.velocity = 0.0f;  // note-off for every voice of the synth
        send_locked(&off);
    }
    int first, n = sampler_region(&ed, frames, -1, &first);
    LightLock_Lock(&s_lock);
    bool ok = load_preset(SAMPLER_PRESET, data + first, n, rate);
    for (int k = 0; ok && k < ed.slices; k++) {
        int sn = sampler_region(&ed, frames, k, &first);
        ok = load_preset(SAMPLER_PRESET + 1 + k, data + first, sn > 0 ? sn : 1, rate);
    }
    LightLock_Unlock(&s_lock);
    if (ok) s_slices = ed.slices;
    return ok;
}

void engine_apply_edit(int part, const SynthEdit *edit) {
    if (part < 0 || part >= PARTS || !edit) return;
    const int sound = s_part_sound[part];
    if (sound_edit_key(sound, edit) != s_part_key[part]) {
        engine_load_sound(part, sound, s_part_patch[part], edit);  // main.c re-sends the macros
    } else {
        send_edit(part, sound, edit);
        if (sound == sound_dx7_index()) {
            const float b = sound_edit_bright(sound, edit);
            if (b != s_bright[part]) {
                s_bright[part] = b;
                s_bright_due[part] = true;
            }
        }
    }
}

void engine_load_wave(int part, const int16_t *pcm, int n) {
    if (part < 0 || part >= PARTS || n <= 0) return;
    // AMY's wavetable oscillator (w19) plays the preset as cycles of n
    // samples, pitched by the note. It wants two (it morphs between them):
    // the same cycle, twice. Overwriting the RAM is heard straight away.
    LightLock_Lock(&s_lock);
    int16_t *ram = pcm_load((uint16_t)(DRAW_PRESET + part), (uint32_t)(2 * n), 44100, 1, 60.0f, 0, 0);
    if (ram) {
        memcpy(ram, pcm, (size_t)n * sizeof(int16_t));
        memcpy(ram + n, pcm, (size_t)n * sizeof(int16_t));
    }
    LightLock_Unlock(&s_lock);
}

void engine_pitch_bend(float semitones) {
    amy_event e = amy_default_event();
    e.pitch_bend = semitones / 12.0f;  // AMY bends in octaves
    send_locked(&e);
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
        case PARAM_TONE:  // on the dry mix (the echo and reverb returns keep their own colour)
            // Tilt EQ around the middle: 0 = dark, 0.5 = flat, 1 = bright (dB).
            e.eq_l = (0.5f - v) * 24.0f;
            e.eq_h = (v - 0.5f) * 24.0f;
            break;
        case PARAM_ECHO:  // how loud the echo comes back (the strips' sends feed it)
            e.bus = MIX_ECHO_BUS;
            e.echo_level = v;
            break;
        case PARAM_REVERB:
            e.bus = MIX_VERB_BUS;
            e.reverb_level = v;
            break;
        case PARAM_VOLUME:  // the dry mix and both returns together
            e.volume = v * 2.5f;
            mixer_set_master_volume(e.volume);
            for (int bus = MIX_ECHO_BUS; bus <= MIX_VERB_BUS; bus++) {
                amy_event r = amy_default_event();
                r.bus = (uint16_t)bus;
                r.volume = e.volume;
                send_locked(&r);
            }
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
    // amy_simple_fill_buffer(), with the oscillators shared between cores.
    amy_grab_render_lock();
    amy_execute_deltas();
    const uint16_t split = s_worker ? amy_split_point(CORE1_SHARE) : AMY_OSCS;
    if (split < AMY_OSCS) {
        s_split = split;
        LightEvent_Signal(&s_go);
        amy_render(0, split, 0);
        LightEvent_Wait(&s_done);
    } else {
        amy_render(0, AMY_OSCS, 0);
        amy_render(AMY_OSCS, AMY_OSCS, 1);  // nothing to render: just clears core 1's buffers
    }
    const int16_t *block = amy_fill_buffer();
    amy_release_render_lock();
    dx7_bright_tick();
    LightLock_Unlock(&s_lock);
    return block;
}

bool engine_two_cores(void) { return s_worker != NULL; }

int engine_block_frames(void) { return AMY_BLOCK_SIZE; }

int engine_sample_rate(void) { return AMY_SAMPLE_RATE; }
