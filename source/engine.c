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
#define PERF_K (1.0f / 64.0f)
static void load_drum_banks(void);

// The second core (the old 3DS's "system core", which an app may use up to
// a time limit): it renders part of every block's oscillators while the
// audio thread renders the rest, then the audio thread mixes and runs the
// effects. See amy_split.h.
static Thread s_worker;
static LightEvent s_go, s_done;
static volatile uint16_t s_split;
static volatile bool s_worker_quit;

// Each block's job for core 1: IDLE -> POSTED (the audio thread) -> TAKEN
// (the worker, before it touches an oscillator) -> IDLE (the worker, done).
// A worker that hasn't taken its job in time (core 1 stalled) loses it: the
// audio thread takes it back (POSTED -> IDLE) and renders that share itself,
// so no oscillator is ever rendered on both cores. One that has taken it is
// running, and is waited for.
enum { JOB_IDLE, JOB_POSTED, JOB_TAKEN };
static int s_job = JOB_IDLE;
static volatile uint32_t s_core1_ticks;  // how long the worker's last share took
#define WORKER_REST_MIN 64      // blocks rendered on core 0 alone after a stall (~0.2 s)...
#define WORKER_REST_MAX 2048    // ...doubling while it keeps stalling (to ~6 s)
static int s_worker_rest, s_worker_rest_len = WORKER_REST_MIN;
static uint32_t s_worker_stalls;

// Core 1's share of the oscillators. Core 0 has much more to do than
// render: AMY's mixdown (seven buses, echo, reverb, EQ, the mixer's hook),
// then PUNCH, the FX and the tape, and the screen besides; core 1 only
// renders. So the split evens out the cores' TOTAL time per block: core 0's
// fixed work counts against it, and core 1 may take every oscillator.
// (Smoothed over ~1 s and moved in steps, so it doesn't wander block to
// block. Any split renders the same sound, bit for bit: tests/dual_probe.)
#define CORE1_SHARE 0.8f
#define CORE1_SHARE_MIN 0.4f
#define CORE1_SHARE_MAX 1.0f
static float s_core1_share = CORE1_SHARE;
static float s_cost0, s_cost1;  // smoothed ticks per sounding oscillator on each core (0: not yet known)
static float s_fixed0;          // smoothed ticks of core 0's other work per block (mixdown + PUNCH / FX / tape)
static uint32_t s_fill_ticks, s_engine_ticks;  // the last block's mixdown, and all of engine_render_block
static EnginePerf s_perf;

static void render_worker(void *arg) {
    (void)arg;
    for (;;) {
        LightEvent_Wait(&s_go);
        if (s_worker_quit) return;
        int expect = JOB_POSTED;
        if (!__atomic_compare_exchange_n(&s_job, &expect, JOB_TAKEN, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;  // taken back while we were held up: nothing to do
        const uint64_t t0 = svcGetSystemTick();
        amy_render(s_split, AMY_OSCS, 1);
        s_core1_ticks = (uint32_t)(svcGetSystemTick() - t0);
        __atomic_store_n(&s_job, JOB_IDLE, __ATOMIC_RELEASE);
        LightEvent_Signal(&s_done);
    }
}

static void balance_cores(uint32_t ticks0, int oscs0, uint32_t ticks1, int oscs1) {
    const float k = 1.0f / 256.0f;  // ~0.75 s of blocks
    if (oscs0 >= 4) {  // (too few to say anything: keep the last estimate)
        const float c0 = (float)ticks0 / oscs0;
        s_cost0 = s_cost0 > 0.0f ? s_cost0 + (c0 - s_cost0) * k : c0;
    }
    if (oscs1 >= 4) {
        const float c1 = (float)ticks1 / oscs1;
        s_cost1 = s_cost1 > 0.0f ? s_cost1 + (c1 - s_cost1) * k : c1;
    }
    const int n = oscs0 + oscs1;
    if (n < 4 || s_cost1 <= 0.0f) return;
    const float c0 = s_cost0 > 0.0f ? s_cost0 : s_cost1;  // core 0 hasn't rendered enough to know: as core 1
    // Even totals: (1 - s) n c0 + fixed = s n c1.
    float want = (n * c0 + s_fixed0) / (n * (c0 + s_cost1));
    want = want < CORE1_SHARE_MIN ? CORE1_SHARE_MIN : want > CORE1_SHARE_MAX ? CORE1_SHARE_MAX : want;
    if (want > s_core1_share + 0.05f || want < s_core1_share - 0.05f)  // a dead band: no hunting
        s_core1_share = (float)(int)(want * 20.0f + 0.5f) / 20.0f;
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

// --- touching AMY from outside the render ---------------------------------
// With the pipeline, core 1 is usually rendering, even between the audio
// thread's calls. So anything that changes the synth (notes, patches,
// samples) waits for the safe point: amy_enter() returns with the lock held
// and nothing rendering. The audio thread parks at its safe point while
// calls are waiting (s_want), so they're never starved; calls nest, so a
// batch (a patch and its edits) goes in at one safe point. The sequencer's
// notes come from the audio thread AT the safe point: nothing to wait for.
static volatile int s_want;
static void *s_audio_tls;
static volatile bool s_audio_inside;
static int s_ui_depth;  // UI thread only
static bool s_in_flight, s_rendered;
static uint32_t s_r0_ticks;
static int s_oscs0, s_oscs1;

static void amy_enter(void) {
    if (s_audio_tls && getThreadLocalStorage() == s_audio_tls) {
        if (!s_audio_inside) LightLock_Lock(&s_lock);  // (not expected: the audio thread calls in at its safe point)
        return;
    }
    if (s_ui_depth++ > 0) return;
    __atomic_add_fetch(&s_want, 1, __ATOMIC_ACQ_REL);
    for (;;) {
        LightLock_Lock(&s_lock);
        if (__atomic_load_n(&s_job, __ATOMIC_ACQUIRE) == JOB_IDLE) return;
        LightLock_Unlock(&s_lock);
        svcSleepThread(100000);  // 0.1 ms
    }
}

static void amy_leave(void) {
    if (s_audio_tls && getThreadLocalStorage() == s_audio_tls) {
        if (!s_audio_inside) LightLock_Unlock(&s_lock);
        return;
    }
    if (--s_ui_depth > 0) return;
    __atomic_sub_fetch(&s_want, 1, __ATOMIC_ACQ_REL);
    LightLock_Unlock(&s_lock);
}

static void send_locked(amy_event *e) {
    amy_enter();
    amy_add_event(e);
    amy_leave();
}

// A thread that does nothing, to ask the kernel whether one is allowed at
// this priority on this core (libctru's threadCreate doesn't say why not).
static char s_core_report[400];
static bool s_worker_core1;
static u8 s_probe_stack[4096] __attribute__((aligned(8)));
static void probe_entry(void *arg) {
    (void)arg;
    svcExitThread();
}
static Result probe_thread(int prio, int core) {
    Handle h = 0;
    const Result rc = svcCreateThread(&h, probe_entry, 0, (u32 *)(s_probe_stack + sizeof(s_probe_stack)), prio, core);
    if (R_SUCCEEDED(rc)) {
        svcWaitSynchronization(h, 200000000LL);
        svcCloseHandle(h);
    }
    return rc;
}

const char *engine_core_report(void) { return s_core_report; }

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
    const Result apt_rc = APT_SetAppCpuTimeLimit(80);
    u32 apt_pct = 0;
    const Result apt_get = APT_GetAppCpuTimeLimit(&apt_pct);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // What the system says to a thread on core 1 (and on "any core"), at a
    // few priorities: kept for the perf log, so a refusal on some console or
    // install type can be seen for what it is.
    const int prios[4] = {prio - 2 < 0x18 ? 0x18 : prio - 2, prio, 0x3F, 0x18};
    int rn = snprintf(s_core_report, sizeof(s_core_report), "apt set %08lX get %08lX %lu%% main prio %ld |", (unsigned long)apt_rc,
                     (unsigned long)apt_get, (unsigned long)apt_pct, (long)prio);
    int pick_core = 0, pick_prio = 0;
    bool found = false;
    static const int cores[2] = {1, -1};
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 4; k++) {
            const Result rc = probe_thread(prios[k], cores[c]);
            if (rn < (int)sizeof(s_core_report))
                rn += snprintf(s_core_report + rn, sizeof(s_core_report) - (size_t)rn, " core %d prio %02X: %08lX", cores[c], prios[k],
                              (unsigned long)rc);
            if (R_SUCCEEDED(rc) && !found) {
                found = true;
                pick_core = cores[c];
                pick_prio = prios[k];
            }
        }
    if (found) s_worker = threadCreate(render_worker, NULL, 64 * 1024, pick_prio, pick_core, false);
    s_worker_core1 = s_worker && pick_core == 1;
    if (rn < (int)sizeof(s_core_report))
        snprintf(s_core_report + rn, sizeof(s_core_report) - (size_t)rn, " | worker: %s (core %d prio %02X)",
                 s_worker ? "started" : "none", pick_core, pick_prio);

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
        amy_enter();
        int16_t *ram = pcm_load(ORGAN_PRESET, ORGAN_CYCLES * TABLE_CYCLE, 44100, 1, 60.0f, 0, 0);
        if (ram) memcpy(ram, organ, sizeof(organ));
        ram = pcm_load(VOX_PRESET, VOX_CYCLES * TABLE_CYCLE, 44100, 1, 60.0f, 0, 0);
        if (ram) memcpy(ram, vox, sizeof(vox));
        amy_leave();
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
        __atomic_store_n(&s_job, JOB_IDLE, __ATOMIC_RELEASE);  // (a share it never took)
    }
    amy_enter();
    amy_stop();
    amy_leave();
}

// The kit as the engine plays it: each lane's note and level (read by
// note-ons from both threads; written under the lock).
static float s_lane_note[DRUM_LANES], s_lane_gain[DRUM_LANES];

static void drum_hit(int note, float velocity) {
    const int lane = drum_lane_of_note(note);
    if (lane < 0) return;
    amy_enter();
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
    amy_leave();
}

void engine_set_kit(const DrumKit *k) {
    char m[48];
    amy_enter();
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        snprintf(m, sizeof(m), "i%dv0p%d", DRUM_SYNTH0 + lane, k->lane[lane].preset);
        wire_locked(m);
    }
    amy_enter();
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        s_lane_note[lane] = drum_lane_note(k, lane);
        s_lane_gain[lane] = drum_lane_gain(k, lane);
    }
    amy_leave();
    amy_leave();
}

bool engine_load_part_sample(int part, const int16_t *data, int frames, int rate) {
    if (part < 0 || part >= PARTS || frames <= 0) return false;
    amy_enter();
    int16_t *ram = pcm_load((uint16_t)(PART_SAMPLE_PRESET + part), (uint32_t)frames, (uint32_t)rate, 1, 60.0f, 0, 0);
    if (ram) memcpy(ram, data, (size_t)frames * sizeof(int16_t));
    amy_leave();
    return ram != NULL;
}

bool engine_load_drum_sample(int lane, const int16_t *data, int frames, int rate) {
    if (lane < 0 || lane >= DRUM_LANES || frames <= 0) return false;
    amy_enter();
    int16_t *ram = pcm_load((uint16_t)(DRUM_USER_PRESET + lane), (uint32_t)frames, (uint32_t)rate, 1, 60.0f, 0, 0);
    if (ram) memcpy(ram, data, (size_t)frames * sizeof(int16_t));
    amy_leave();
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
    amy_enter();
    amy_add_message(buf);
    amy_leave();
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
// All of these are written and read with the AMY lock held: a part's flags
// change only after its new patch is queued, so the next render loads the
// patch before dx7_bright_tick reads its levels.
static float s_bright[PARTS] = {1.0f, 1.0f, 1.0f};
static bool s_bright_due[PARTS];  // apply s_bright after the next render
static float s_dx7_level[PARTS][6];
static bool s_dx7_known[PARTS];   // s_dx7_level holds this patch's levels

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

static void engine_load_sound_in(int part, int sound, int patch, const SynthEdit *edit);

static void send_edit(int part, int sound, const SynthEdit *e) {
    char cmds[16][64];
    const int n = sound_edit_wire(sound, e, part_synth(part), &cmds[0][0], 64, 16);
    amy_enter();
    for (int i = 0; i < n; i++) wire_locked(cmds[i]);
    amy_leave();
}

void engine_load_sound(int part, int sound, int patch, const SynthEdit *edit) {
    amy_enter();  // the patch and everything sent after it: one safe point
    engine_load_sound_in(part, sound, patch, edit);
    amy_leave();
}

static void engine_load_sound_in(int part, int sound, int patch, const SynthEdit *edit) {
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
        amy_enter();
        s_bright_due[part] = false;  // nothing for the old patch from here on
        amy_leave();
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
    // BRIGHT: now that the patch is queued (a render in between would have
    // read the old patch's levels and kept them for the new one).
    if (part >= 0 && part < PARTS) {
        amy_enter();
        s_dx7_known[part] = false;  // a new patch: its own levels are read again
        s_bright[part] = sound_edit_bright(sound, edit);
        s_bright_due[part] = sound == sound_dx7_index() && s_bright[part] != 1.0f;
        amy_leave();
    }
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
    amy_enter();
    bool ok = load_preset(SAMPLER_PRESET, data + first, n, rate);
    for (int k = 0; ok && k < ed.slices; k++) {
        int sn = sampler_region(&ed, frames, k, &first);
        ok = load_preset(SAMPLER_PRESET + 1 + k, data + first, sn > 0 ? sn : 1, rate);
    }
    amy_leave();
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
            amy_enter();
            if (b != s_bright[part]) {
                s_bright[part] = b;
                s_bright_due[part] = true;
            }
            amy_leave();
        }
    }
}

void engine_load_wave(int part, const int16_t *pcm, int n) {
    if (part < 0 || part >= PARTS || n <= 0) return;
    // AMY's wavetable oscillator (w19) plays the preset as cycles of n
    // samples, pitched by the note. It wants two (it morphs between them):
    // the same cycle, twice. Overwriting the RAM is heard straight away, so
    // while drawing (many times a second) the preset already there is
    // written over in place: no free and malloc under the lock.
    const uint16_t preset = (uint16_t)(DRAW_PRESET + part);
    amy_enter();
    uint32_t frames = 0, rate = 0;
    uint8_t channels = 0;
    int16_t *ram = (int16_t *)pcm_get_memory_preset(preset, &frames, &channels, &rate, NULL);
    if (!ram || frames != (uint32_t)(2 * n) || channels != 1 || rate != 44100)
        ram = pcm_load(preset, (uint32_t)(2 * n), 44100, 1, 60.0f, 0, 0);
    if (ram) {
        memcpy(ram, pcm, (size_t)n * sizeof(int16_t));
        memcpy(ram + n, pcm, (size_t)n * sizeof(int16_t));
    }
    amy_leave();
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

// The pipeline. Each call hands back the block whose oscillators were
// rendered during the LAST call, and sets the next one going:
//
//   1. wait for core 1 to finish its share of this block (it has had since
//      the last call), merge it with ours, move AMY's clock on;
//   2. the safe point: nothing is rendering. Anything that changes the
//      synth happens here: waiting UI calls (amy_enter), the sequencer's
//      notes (`safe_point`), then AMY's due deltas;
//   3. hand core 1 its share of the NEXT block, and while it renders:
//   4. mix this block down (buses, echo, reverb, the mixer), and
//   5. render our own share of the next block.
//
// So the cores work at the same time on different blocks, and the block's
// wall time is the longer of the two, not their sum. The sound is the same
// as rendering in order (tests/dual_probe), one block (4 ms) later.
const int16_t *engine_render_block(void (*safe_point)(void)) {
    const uint64_t t_block = svcGetSystemTick();
    if (!s_audio_tls) s_audio_tls = getThreadLocalStorage();
    LightLock_Lock(&s_lock);
    amy_grab_render_lock();
    vm_amy_pipeline = 1;

    // 1. This block's render, finished and merged.
    if (s_in_flight) {
        // Core 1 has had a whole block's time already; a little more, then
        // if it never even started, its share is rendered here (into its
        // buffers, which nothing else is touching) and it's left alone for
        // a while.
        if (LightEvent_WaitTimeout(&s_done, 500000LL) != 0) {
            int expect = JOB_POSTED;
            if (__atomic_compare_exchange_n(&s_job, &expect, JOB_IDLE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                amy_render(s_split, AMY_OSCS, 1);
                s_worker_rest = s_worker_rest_len;
                if (s_worker_rest_len < WORKER_REST_MAX) s_worker_rest_len *= 2;
                s_worker_stalls++;
            } else {
                LightEvent_Wait(&s_done);  // it's rendering: let it finish
                s_worker_rest_len = WORKER_REST_MIN;
            }
        } else {
            s_worker_rest_len = WORKER_REST_MIN;
        }
        balance_cores(s_r0_ticks, s_oscs0, s_core1_ticks, s_oscs1);
    } else {
        amy_render(AMY_OSCS, AMY_OSCS, 1);  // core 1 rendered nothing: clear its buffers
    }
    vm_amy_merge_cores();
    if (s_rendered) vm_amy_advance_time();

    // 2. The safe point.
    while (__atomic_load_n(&s_want, __ATOMIC_ACQUIRE) > 0) {  // UI calls waiting: let them in
        LightLock_Unlock(&s_lock);
        svcSleepThread(50000);
        LightLock_Lock(&s_lock);
    }
    s_audio_inside = true;
    if (safe_point) safe_point();
    dx7_bright_tick();
    s_audio_inside = false;
    amy_execute_deltas();

    // 3. Core 1's share of the next block.
    int oscs0 = 0, oscs1 = 0;
    const bool worker = s_worker && (s_worker_rest == 0 || --s_worker_rest == 0);
    const uint16_t split = worker ? amy_split_point_n(s_core1_share, &oscs0, &oscs1) : AMY_OSCS;
    if (!worker) amy_split_point_n(0.0f, &oscs0, &oscs1);  // (just the count)
    s_in_flight = split < AMY_OSCS;
    if (s_in_flight) {
        s_split = split;
        __atomic_store_n(&s_job, JOB_POSTED, __ATOMIC_RELEASE);
        LightEvent_Signal(&s_go);
    }
    s_oscs0 = oscs0;
    s_oscs1 = oscs1;

    // 4. This block, mixed down.
    const uint64_t tf = svcGetSystemTick();
    const int16_t *block = amy_fill_buffer();
    s_fill_ticks = (uint32_t)(svcGetSystemTick() - tf);

    // 5. Our share of the next block.
    const uint64_t t0 = svcGetSystemTick();
    amy_render(0, split, 0);
    s_r0_ticks = (uint32_t)(svcGetSystemTick() - t0);
    s_rendered = true;

    amy_release_render_lock();
    LightLock_Unlock(&s_lock);
    s_engine_ticks = (uint32_t)(svcGetSystemTick() - t_block);
    s_perf.render0 += ((float)s_r0_ticks - s_perf.render0) * PERF_K;
    s_perf.core1 += ((float)(s_in_flight ? s_core1_ticks : 0) - s_perf.core1) * PERF_K;
    s_perf.fill += ((float)s_fill_ticks - s_perf.fill) * PERF_K;
    s_perf.oscs += ((float)(oscs0 + oscs1) - s_perf.oscs) * PERF_K;
    return block;
}

void engine_batch_begin(void) { amy_enter(); }
void engine_batch_end(void) { amy_leave(); }

void engine_block_done(uint32_t total_ticks) {
    // Everything core 0 did this block that wasn't rendering oscillators.
    const uint32_t post = total_ticks > s_engine_ticks ? total_ticks - s_engine_ticks : 0;
    const float fixed = (float)(s_fill_ticks + post);
    s_fixed0 = s_fixed0 > 0.0f ? s_fixed0 + (fixed - s_fixed0) * (1.0f / 256.0f) : fixed;
    s_perf.post += ((float)post - s_perf.post) * PERF_K;
    s_perf.total += ((float)total_ticks - s_perf.total) * PERF_K;
    if ((float)total_ticks > s_perf.total_max) s_perf.total_max = (float)total_ticks;
}

EnginePerf engine_perf(void) {
    EnginePerf p = s_perf;
    const float block = (float)AMY_BLOCK_SIZE / AMY_SAMPLE_RATE * SYSCLOCK_ARM11;  // ticks in a block's time
    p.render0 /= block, p.core1 /= block, p.fill /= block, p.post /= block, p.total /= block, p.total_max /= block;
    p.share = s_core1_share;
    p.stalls = s_worker_stalls;
    s_perf.total_max = 0.0f;
    return p;
}

bool engine_two_cores(void) { return s_worker_core1; }  // (a worker that had to settle for core 0 doesn't count)

uint32_t engine_core1_stalls(void) { return s_worker_stalls; }

float engine_core1_share(void) { return s_core1_share; }

int engine_block_frames(void) { return AMY_BLOCK_SIZE; }

int engine_sample_rate(void) { return AMY_SAMPLE_RATE; }
