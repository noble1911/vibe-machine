// Vibe Machine -- a pocket synth / groovebox for the Nintendo 3DS.
//
// main.c owns the frame loop and turns buttons and touches into engine and
// sequencer calls. Sound is rendered on a separate thread (audio_out.c),
// which also runs the sequencer clock (seq.c); drawing lives in ui*.c.

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "audio_out.h"
#include "engine.h"
#include "music.h"
#include "patch_names.h"
#include "presets.h"
#include "dice.h"
#include "mic.h"
#include "sampler.h"
#include "samples.h"
#include "songs.h"
#include "seq.h"
#include "storage.h"
#include "tape.h"
#include "wav.h"

// Save states hold eight patterns of up to four bars: give the main thread
// room for a few of them on its stack.
u32 __stacksize__ = 256 * 1024;
#include "ui.h"

#define QUIT_HOLD_FRAMES 60   // hold START this long to quit
#define PLAY_TAP_FRAMES 40    // a shorter press toggles play
#define REPEAT_DELAY 24       // BPM buttons auto-repeat after this...
#define REPEAT_EVERY 3        // ...every this many frames
#define FLASH_FRAMES 6        // keys light up when the sequencer hits them

// Physical buttons for keys 0..7. Each cluster is read left, down, right,
// up, so the D-pad and the face buttons share one shape.
static const u32 kKeyButtons[MUSIC_KEYS] = {
    KEY_DLEFT, KEY_DDOWN, KEY_DRIGHT, KEY_DUP, KEY_Y, KEY_B, KEY_A, KEY_X,
};

// A sound source that can hold notes: one per key button, plus the touch
// keyboard. Each remembers exactly what it started, so changing mode or
// octave mid-note never leaves a note stuck.
#define SOURCE_TOUCH MUSIC_KEYS
#define NUM_SOURCES (MUSIC_KEYS + 1)

typedef struct {
    int key;  // -1 when idle
    int synth;
    bool arp;  // feeding the arpeggiator rather than sounding itself
    int part;  // the part it was played on (for live recording)
    int count;
    int notes[MUSIC_MAX_NOTES];
} Held;

// What the current touch is doing; decided when the stylus lands.
typedef enum {
    TOUCH_NONE,
    TOUCH_IGNORE,
    TOUCH_SLIDER,
    TOUCH_KEYS,
    TOUCH_CELLS,
    TOUCH_BUTTON,
    TOUCH_SYNTH_PAD,
    TOUCH_BANK,
    TOUCH_FX_PAD,
    TOUCH_REC,
    TOUCH_TAPE_SCRUB,
    TOUCH_TAPE_SPEED,
    TOUCH_TAPE_TRACK,
    TOUCH_SLOT,
    TOUCH_SWING,
    TOUCH_SONG_HOLD,
    TOUCH_ROLL_NOTE,
    TOUCH_ROLL_KEYS,
    TOUCH_PART_HOLD,
    TOUCH_MIX,
    TOUCH_SMP,
    TOUCH_SONG_BTN,
    TOUCH_BAR_BTN,
    TOUCH_EDIT_ENV,     // dragging an envelope handle (s_app.edit_grab)
    TOUCH_EDIT_PAD,     // the FILT / LFO XY pad
    TOUCH_EDIT_SLIDER,  // a slider on the EDIT view (s_edit_slider)
    TOUCH_DRAW,         // drawing the DRAW sound's wave
    TOUCH_KIT_SLIDER,   // KIT: TUNE or LEVEL (s_app.kit_grab)
    TOUCH_KIT_STEP,     // KIT: holding a sample arrow (repeats)
} TouchMode;

#define BAR_LEN_FRAMES 30  // hold BAR this long to change the pattern's length

#define UNDO_HOLD_FRAMES 40   // hold the SEQ page's ✕ this long to undo
#define SONG_SHEET_FRAMES 30  // hold SONG this long to open / close the SONG sheet

#define PART_MUTE_FRAMES 40  // hold a SYNTH part sticker this long to (un)mute it

#define SONG_HOLD_FRAMES 45  // hold DELETE / NEW (and LOAD over unsaved work)

#ifdef VM_DEMO
#define SONGS_ROOT STORAGE_DIR "/demo-songs"  // the attract build never touches your songs
#else
#define SONGS_ROOT STORAGE_DIR "/songs"
#endif

#define SLOT_COPY_FRAMES 40  // hold a slot sticker this long to copy into it

#define TAPE_WIPE_FRAMES 45  // hold a track sticker this long to wipe it

static Held s_held[NUM_SOURCES];
static AppState s_app;
static int s_theme;

static TouchMode s_touch;

// The grid and the roll show one bar of the pattern: screen column c is
// step page_step(c).
static inline int page_step(int column) { return s_app.bar_page * SEQ_STEPS + column; }

static touchPosition s_tp;  // this frame's stylus (real, or the story's)
static u32 s_keys_held;     // this frame's held buttons (real, or the story's)
static SeqButton s_button;
static unsigned s_button_frames;
static int s_paint_row, s_paint_step;
static bool s_paint_on;

static SeqNote s_last_note = {0, 1, {MUSIC_ROOT}};  // painted into the synth row
static unsigned s_start_down;
static uint32_t s_seen_fired;
static int s_flash[MUSIC_KEYS];

static int next_theme(void) {
    s_theme = (s_theme + 1) % ui_theme_count();
    return s_theme;
}

// --------------------------------------------------------------- notes ----

// Scroll the piano roll (if needed) so `pitch` is on screen.
static void roll_show_pitch(int pitch) {
    int row = music_roll_row(pitch, s_app.roll_chromatic);
    if (row < s_app.roll_scroll) s_app.roll_scroll = row;
    if (row >= s_app.roll_scroll + ROLL_ROWS) s_app.roll_scroll = row - ROLL_ROWS + 1;
}

// The arpeggiator plays whatever is held for it, across every source.
static void arp_update(void) {
    int notes[NUM_SOURCES * MUSIC_MAX_NOTES], n = 0;
    for (int s = 0; s < NUM_SOURCES; s++)
        if (s_held[s].key >= 0 && s_held[s].arp)
            for (int i = 0; i < s_held[s].count; i++) notes[n++] = s_held[s].notes[i];
    seq_arp_hold(s_app.part, notes, n);
}

static void source_release(int src) {
    Held *h = &s_held[src];
    if (h->key < 0) return;
    if (h->arp) {  // it was the arpeggiator's to play
        h->key = -1;
        h->count = 0;
        h->arp = false;
        arp_update();
        return;
    }
    for (int i = 0; i < h->count; i++) engine_note_off(h->synth, h->notes[i]);
    if (h->synth != SYNTH_DRUMS) {  // a live-recorded note is as long as you held it
        SeqNote n = {(int8_t)h->key, (uint8_t)h->count, {0}};
        for (int i = 0; i < h->count && i < MUSIC_MAX_NOTES; i++) n.notes[i] = (uint8_t)h->notes[i];
        seq_record_release(h->part, &n);
    }
    h->key = -1;
    h->count = 0;
}

static void source_press(int src, int key) {
    source_release(src);
    Held *h = &s_held[src];
    h->key = key;
    h->synth = s_app.mode == PLAY_DRUM ? SYNTH_DRUMS : part_synth(s_app.part);
    h->part = s_app.part;
    h->count = music_key_notes(key, s_app.mode, s_app.octave, h->notes);
    h->arp = s_app.arp.on && s_app.mode != PLAY_DRUM;
    if (h->arp) {  // the arpeggiator plays it (and records it, if recording)
        arp_update();
        if (s_app.roll_view && s_app.page == PAGE_SEQ && h->count > 0) roll_show_pitch(h->notes[0]);
        ui_key_struck(key, s_app.mode);
        SeqNote n = {(int8_t)key, (uint8_t)h->count, {0}};
        for (int i = 0; i < h->count; i++) n.notes[i] = (uint8_t)h->notes[i];
        s_last_note = n;
        return;
    }
    float velocity = s_app.mode == PLAY_DRUM ? 1.0f : 0.8f;
    for (int i = 0; i < h->count; i++) engine_note_on(h->synth, h->notes[i], velocity);
    if (h->synth == SYNTH_DRUMS && key >= 0 && key < DRUM_LANES) s_app.drum_flash[key] = 8;
    if (s_app.roll_view && s_app.page == PAGE_SEQ && h->synth != SYNTH_DRUMS && h->count > 0)
        roll_show_pitch(h->notes[0]);  // bring what you play into view
    ui_key_struck(key, s_app.mode);

    // Remember it for painting into the grid, and record it if armed.
    if (s_app.mode == PLAY_DRUM) {
        seq_record_drum(key);
    } else {
        SeqNote n = {(int8_t)key, (uint8_t)h->count, {0}};
        for (int i = 0; i < h->count; i++) n.notes[i] = (uint8_t)h->notes[i];
        s_last_note = n;
        seq_record_synth(s_app.part, &n);
    }
}

static void release_all(void) {
    for (int s = 0; s < NUM_SOURCES; s++) source_release(s);
}

// Juno patches carry their own EQ/chorus settings, so after any sound or
// patch load the SOUND sliders are sent again to keep what's on screen true.
static void reapply_params(void) {
    for (int i = 0; i < PARAM_COUNT; i++) engine_set_param((EngineParam)i, s_app.params[i]);
}

static void set_macro(int m, float v) {
    if (fabsf(v - s_app.macro[m]) < 0.01f) return;
    s_app.macro[m] = v;
    engine_set_macro(s_app.part, s_app.sound, m, v);
}

// The song's key, and the arpeggiator's settings.
static void release_all(void);
static void set_key(int root, int scale) {
    release_all();  // held notes belong to the old key
    music_set_key(root, scale);
    s_app.key_root = music_root();
    s_app.key_scale = music_scale();
}

static void set_arp(const SeqArp *a) {
    release_all();
    seq_arp_hold(s_app.part, NULL, 0);
    s_app.arp = *a;
    if (s_app.arp.mode >= ARP_MODES) s_app.arp.mode = ARP_UP;
    if (s_app.arp.rate >= ARP_RATES) s_app.arp.rate = ARP_16TH;
    if (s_app.arp.octaves < 1 || s_app.arp.octaves > 2) s_app.arp.octaves = 1;
    seq_set_arp(&s_app.arp);
}

static void sound_row(int hit) {
    SeqArp a = s_app.arp;
    switch (hit) {
        case SND_HIT_ROOT_DOWN: set_key(s_app.key_root + 11, s_app.key_scale); break;
        case SND_HIT_ROOT_UP: set_key(s_app.key_root + 1, s_app.key_scale); break;
        case SND_HIT_SCALE_PREV: set_key(s_app.key_root, (s_app.key_scale + MUSIC_SCALES - 1) % MUSIC_SCALES); break;
        case SND_HIT_SCALE_NEXT: set_key(s_app.key_root, (s_app.key_scale + 1) % MUSIC_SCALES); break;
        case SND_HIT_ARP_ON: a.on = !a.on; set_arp(&a); break;
        case SND_HIT_ARP_MODE: a.mode = (uint8_t)((a.mode + 1) % ARP_MODES); set_arp(&a); break;
        case SND_HIT_ARP_RATE: a.rate = (uint8_t)((a.rate + 1) % ARP_RATES); set_arp(&a); break;
        case SND_HIT_ARP_OCT: a.octaves = a.octaves == 1 ? 2 : 1; set_arp(&a); break;
        default: break;
    }
}

// ----------------------------------------------------------------- EDIT ----

static PartEdit *part_edit(void) { return &s_app.parts[s_app.part].pe; }
static uint32_t s_edit_key[PARTS];  // the structure each part's patch was loaded with

static void upload_wave(int part) {
    static int16_t cycle[DRAW_CYCLE];
    DrawWave *w = &s_app.parts[part].wave;
    bool empty = true;
    for (int i = 0; i < DRAW_POINTS; i++) empty = empty && w->pt[i] == 0;
    if (empty) draw_preset(w, 1);  // a saw to start from
    draw_render(w, cycle);
    engine_load_wave(part, cycle, DRAW_CYCLE);
}

// Send the current part's sound again: patch, macros, then the EDIT layer
// on top (so the edit wins where both touch the same thing).
static void reload_sound(void) {
    const SynthEdit *e = &part_edit()->cur;
    engine_load_sound(s_app.part, s_app.sound, s_app.patch, e);
    s_edit_key[s_app.part] = sound_edit_key(s_app.sound, e);
    if (s_app.sound == sound_draw_index()) upload_wave(s_app.part);
    for (int m = 0; m < SOUND_MACROS; m++) engine_set_macro(s_app.part, s_app.sound, m, s_app.macro[m]);
    engine_apply_edit(s_app.part, e);
    reapply_params();
}

// The EDIT layer changed: live, unless MONO / UNISON / the LFO's extra
// oscillator changed the patch's shape (then it's reloaded, notes let go).
static void apply_edit(void) {
    const SynthEdit *e = &part_edit()->cur;
    if (sound_edit_key(s_app.sound, e) != s_edit_key[s_app.part]) {
        release_all();
        reload_sound();
    } else {
        engine_apply_edit(s_app.part, e);
    }
}

static void edit_reset_sound(void) {
    float d[ED_PARAMS];
    sound_edit_defaults(s_app.sound, d);
    PartEdit *pe = part_edit();
    edit_reset(&pe->cur, d);
    pe->snap[0] = pe->snap[1] = pe->cur;
    pe->snaps = 0;
    pe->morph = 0.0f;
}

static void select_sound(int index) {
    int n = sound_count();
    index = ((index % n) + n) % n;
    const Sound *snd = sound_get(index);
    release_all();
    s_app.sound = index;
    if (snd->kind == SOUND_BANK) s_app.patch = snd->bank_first;
    for (int m = 0; m < SOUND_MACROS; m++) s_app.macro[m] = snd->macro_default[m];
    edit_reset_sound();  // a new sound starts as designed
    reload_sound();
}

// Step through the current BANK sound's patches (wrapping inside the bank).
static void change_patch(int patch) {
    const Sound *snd = sound_get(s_app.sound);
    if (snd->kind != SOUND_BANK) return;
    int first = snd->bank_first, n = snd->bank_count;
    patch = first + (((patch - first) % n) + n) % n;
    release_all();
    s_app.patch = patch;
    reload_sound();  // keeps the EDIT layer: try the same shaping on another patch
}

// ---------------------------------------------------------------- parts ----

// Part 1 JUNO, part 2 PLUCK, part 3 BELL: three different voices to start.
static const int kPartDefault[PARTS] = {0, 2, 4};

static void sync_part_out(void) {  // working copy -> the part's table
    PartState *ps = &s_app.parts[s_app.part];
    ps->sound = s_app.sound;
    ps->patch = s_app.patch;
    memcpy(ps->macro, s_app.macro, sizeof(ps->macro));
}

// Make `part` the one the keys, roll and SYNTH page work on. Each part's
// synth keeps its own sound, so nothing reloads.
static void switch_part(int part) {
    if (part < 0 || part >= PARTS) return;
    sync_part_out();
    release_all();
    s_app.part = part;
    const PartState *ps = &s_app.parts[part];
    s_app.sound = ps->sound;
    s_app.patch = ps->patch;
    memcpy(s_app.macro, ps->macro, sizeof(s_app.macro));
}

// Load a sound (patch and macros optional) onto a part.
static void load_part(int part, int sound, int patch, const float *macro, const PartEdit *pe, const DrawWave *wave) {
    switch_part(part);
    select_sound(sound);
    const Sound *snd = sound_get(s_app.sound);
    bool again = false;
    if (snd->kind == SOUND_BANK && patch >= snd->bank_first && patch < snd->bank_first + snd->bank_count) {
        s_app.patch = patch;
        again = true;
    }
    if (macro) {
        memcpy(s_app.macro, macro, sizeof(s_app.macro));
        again = true;
    }
    if (pe) {
        *part_edit() = *pe;
        edit_clamp(&part_edit()->cur);
        edit_clamp(&part_edit()->snap[0]);
        edit_clamp(&part_edit()->snap[1]);
        again = true;
    }
    if (wave) {
        s_app.parts[part].wave = *wave;
        again = true;
    }
    if (again) reload_sound();
    sync_part_out();
}

static void default_parts(void) {
    for (int k = PARTS - 1; k >= 0; k--) {
        load_part(k, kPartDefault[k], -1, NULL, NULL, NULL);
        upload_wave(k);
        seq_set_part_mute(k, false);
    }
}

// ---------------------------------------------------------------- MIX ----
// Mutes live in the sequencer (so a muted part's notes aren't even sent);
// solos and the strips' settings live here. Both reach the engine through
// mix_apply().

#define MIX_DOUBLE_TAP_FRAMES 20  // tap a knob or fader twice to reset it

static uint8_t s_mix_audible = 0xFF;  // what the engine was last told
static int s_mix_start_y;
static float s_mix_start_v;
static int s_mix_last_tap = -1;  // strip * 8 + what, for double taps
static unsigned s_mix_last_tap_frame;

static bool mix_audible(int k) {
    if ((seq_status().muted >> k) & 1u) return false;
    return s_app.mix_solo == 0 || ((s_app.mix_solo >> k) & 1u);
}

static void mix_apply(int k) { engine_set_strip(k, &s_app.mix[k], mix_audible(k)); }

static void mix_apply_all(void) {
    s_mix_audible = 0;
    for (int k = 0; k < MIX_STRIPS; k++) {
        mix_apply(k);
        if (mix_audible(k)) s_mix_audible |= (uint8_t)(1u << k);
    }
}

// A new song's strip: as mixer_strip_default(), except that the drums stay
// out of the echo and get just a little reverb.
static void strip_default(int k, MixStrip *s) {
    mixer_strip_default(s);
    if (k == MIX_DRUMS) {
        s->knob[MIX_ECHO] = 0.0f;
        s->knob[MIX_VERB] = 0.3f;
    }
}

static void mix_default(void) {
    for (int k = 0; k < MIX_STRIPS; k++) strip_default(k, &s_app.mix[k]);
    s_app.mix_solo = 0;
    seq_set_part_mute(MIX_DRUMS, false);
    mix_apply_all();
}

// Once a frame: mutes change from several places (here, the SYNTH page's
// part stickers, loading a song), so notice and pass them on; and run the
// meters.
static void mix_update(void) {
    uint8_t mask = 0;
    for (int k = 0; k < MIX_STRIPS; k++)
        if (mix_audible(k)) mask |= (uint8_t)(1u << k);
    if (mask != s_mix_audible) mix_apply_all();
    for (int k = 0; k < MIX_STRIPS; k++) {
        float m = sqrtf(mixer_peak(k) / 10.0f);  // ~10 is full scale at the default volume
        if (m > 1.0f) m = 1.0f;
        float fall = s_app.mix_meter[k] * 0.88f;
        s_app.mix_meter[k] = m > fall ? m : fall;
    }
}

static float mix_default_value(int k, int grab) {
    MixStrip d;
    strip_default(k, &d);
    return grab == MIX_GRAB_FADER ? d.level : d.knob[grab - MIX_GRAB_KNOB];
}

static float *mix_value(int k, int grab) {
    return grab == MIX_GRAB_FADER ? &s_app.mix[k].level : &s_app.mix[k].knob[grab - MIX_GRAB_KNOB];
}

static void mix_begin(int x, int y) {
    int k = -1;
    const int hit = ui_hit_mix(x, y, &k);
    s_touch = TOUCH_IGNORE;
    if (hit == MIX_HIT_NONE) return;
    if (hit == MIX_HIT_HEAD) {
        if (k < PARTS) switch_part(k);  // play this part from the keys
        return;
    }
    if (hit == MIX_HIT_MUTE) {
        seq_set_part_mute(k, !((seq_status().muted >> k) & 1u));
        return;
    }
    if (hit == MIX_HIT_SOLO) {
        s_app.mix_solo ^= (uint8_t)(1u << k);
        return;
    }
    const int grab = hit == MIX_HIT_FADER ? MIX_GRAB_FADER : MIX_GRAB_KNOB + (hit - MIX_HIT_KNOB);
    const int tap = k * 8 + grab + 1;
    if (tap == s_mix_last_tap && s_app.frame - s_mix_last_tap_frame < MIX_DOUBLE_TAP_FRAMES) {
        *mix_value(k, grab) = mix_default_value(k, grab);  // double tap: back to the start
        mix_apply(k);
        s_mix_last_tap = -1;
        return;
    }
    s_mix_last_tap = tap;
    s_mix_last_tap_frame = s_app.frame;
    s_app.mix_strip = k;
    s_app.mix_grab = grab;
    s_mix_start_y = y;
    s_mix_start_v = *mix_value(k, grab);
    s_touch = TOUCH_MIX;
}

static void mix_drag(int y) {
    const int k = s_app.mix_strip, grab = s_app.mix_grab;
    if (k < 0 || grab == MIX_GRAB_NONE) return;
    float v;
    if (grab == MIX_GRAB_FADER) {
        // Relative, like the knobs: touching the cap doesn't make it jump.
        v = s_mix_start_v + (s_mix_start_y - y) / (float)(MIX_FADER_H - 10);
    } else {
        v = s_mix_start_v + (s_mix_start_y - y) / MIX_KNOB_DRAG;
        const int knob = grab - MIX_GRAB_KNOB;
        if ((knob == MIX_TONE || knob == MIX_PAN) && fabsf(v - 0.5f) < 0.04f) v = 0.5f;  // a detent in the middle
    }
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    if (v != *mix_value(k, grab)) {
        *mix_value(k, grab) = v;
        mix_apply(k);
    }
}

// --------------------------------------------------------------- TAPE ----

static bool s_tape_armed;
static float s_tape_speed = 1.0f;
static int s_scrub_last_x;
static float s_scrub_v;
static unsigned s_tape_hold_frames;

static void restart_sequencer(void) {
    if (s_app.seq.playing) seq_toggle_play();
    seq_toggle_play();  // starts again from step 1
}

// START. With SEQ LINK on, the tape rolls with the beat (from the top, as
// the sequencer does), so taped layers play along.
static void start_pressed(void) {
    const bool play = !seq_status().playing;
    seq_toggle_play();
    if (!s_app.tape_link) return;
    if (play) {
        tape_rewind();
        tape_play(true);
    } else {
        tape_play(false);
    }
}

static void tape_button(int hit) {
    switch (hit) {
        case TAPE_HIT_REW:
            tape_rewind();
            if (s_app.tape_link && s_app.seq.playing) restart_sequencer();
            break;
        case TAPE_HIT_PLAY: {
            bool play = !s_app.tape.playing;
            tape_play(play);
            // Linked, the tape and the sequencer start together on the same
            // audio block, so recordings land on the beat.
            if (s_app.tape_link) {
                if (play) restart_sequencer();
                else if (s_app.seq.playing) seq_toggle_play();
            }
            break;
        }
        case TAPE_HIT_REC:
            s_tape_armed = !s_tape_armed;
            tape_record(s_tape_armed);
            break;
        case TAPE_HIT_LINK: s_app.tape_link = !s_app.tape_link; break;
        default: break;
    }
}

// Tape tracks are WAV files next to the state file. They are written when
// the tape stops (and on quit), never mid-performance.
static void tape_path(char *out, size_t len, int track) { snprintf(out, len, STORAGE_DIR "/tape%d.wav", track + 1); }

static void save_tape(void) {
    for (int t = 0; t < TAPE_TRACKS; t++) {
        if (!tape_dirty(t)) continue;
        char path[64];
        tape_path(path, sizeof(path), t);
        int frames = 0;
        const int16_t *data = tape_track_data(t, &frames);
        bool ok = frames > 0 ? wav_write(path, data, frames, TAPE_RATE) : (remove(path), true);
        if (ok) tape_mark_saved(t);
    }
}

static void load_tape(void) {
    int16_t *buf = malloc(TAPE_FRAMES * sizeof(int16_t));
    if (!buf) return;
    for (int t = 0; t < TAPE_TRACKS; t++) {
        char path[64];
        tape_path(path, sizeof(path), t);
        int rate = 0, frames = wav_read(path, buf, TAPE_FRAMES, &rate);
        if (frames > 0 && rate == TAPE_RATE) tape_load_track(t, buf, frames);
    }
    free(buf);
}

// ---------------------------------------------------------------- MIC ----

static float s_take_secs;  // length of the loaded take
// A copy of the loaded take, so a song can save it; s_take_user is false
// while it is still the built-in boing.
static void slices_to_kit(void);
static int16_t s_take_buf[SAMPLER_MAX_FRAMES];
static int s_take_frames;

// ----------------------------------------------------------- drum kit ----

// Each lane's own sample (a lane set to DRUM_USER_PRESET + lane): kept here
// so it can be saved with the song, and in AMY to play.
#define DRUM_OWN_MAX (SAMPLER_RATE * 2)
static int16_t s_lane_buf[DRUM_LANES][DRUM_OWN_MAX];
static int s_lane_frames[DRUM_LANES];
static uint32_t s_kit_gen, s_song_kit_gen;  // bumps when a lane's own sample changes

static bool lane_own(int lane) { return s_app.kit.lane[lane].preset == DRUM_USER_PRESET + lane; }

static void apply_kit(void) {
    drum_kit_clamp(&s_app.kit);
    engine_set_kit(&s_app.kit);
}

static bool save_lanes(const char *dir);

// Put `frames` of sample on `lane` as its own sound (trimmed to 2 s).
static bool lane_take(int lane, const int16_t *data, int frames) {
    if (lane < 0 || lane >= DRUM_LANES || frames <= 0) return false;
    if (frames > DRUM_OWN_MAX) frames = DRUM_OWN_MAX;
    memmove(s_lane_buf[lane], data, (size_t)frames * sizeof(int16_t));
    s_lane_frames[lane] = frames;
    if (!engine_load_drum_sample(lane, s_lane_buf[lane], frames, SAMPLER_RATE)) return false;
    drum_lane_use_own(&s_app.kit, lane);
    apply_kit();
    s_kit_gen++;
#ifndef VM_DEMO
    save_lanes(STORAGE_DIR);  // the autosave keeps it too
#endif
    return true;
}

static void lane_wav_path(char *out, size_t len, const char *dir, int lane) {
    snprintf(out, len, "%s/drum%d.wav", dir, lane + 1);
}

// Each lane's own sample to / from a folder (the song's, or the autosave's).
static bool save_lanes(const char *dir) {
    bool ok = true;
    for (int l = 0; l < DRUM_LANES; l++) {
        char path[160];
        lane_wav_path(path, sizeof(path), dir, l);
        if (lane_own(l) && s_lane_frames[l] > 0)
            ok &= wav_write(path, s_lane_buf[l], s_lane_frames[l], SAMPLER_RATE);
        else
            remove(path);
    }
    return ok;
}

static void load_lanes(const char *dir) {
    for (int l = 0; l < DRUM_LANES; l++) {
        s_lane_frames[l] = 0;
        if (!lane_own(l)) continue;
        char path[160];
        lane_wav_path(path, sizeof(path), dir, l);
        int rate = 0, n = wav_read(path, s_lane_buf[l], DRUM_OWN_MAX, &rate);
        if (n > 0 && rate == SAMPLER_RATE && engine_load_drum_sample(l, s_lane_buf[l], n, SAMPLER_RATE))
            s_lane_frames[l] = n;
        else  // gone: back to the stock kit's sample
            s_app.kit.lane[l] = (DrumLane){0, 0, 100}, drum_kit_clamp(&s_app.kit);
    }
    apply_kit();
}
static bool s_take_user;
static uint32_t s_mic_gen;  // bumps with every new take (for "unsaved")

static void show_take(const int16_t *data, int frames) {
    if (frames > SAMPLER_MAX_FRAMES) frames = SAMPLER_MAX_FRAMES;
    if (data != s_take_buf) memcpy(s_take_buf, data, (size_t)frames * sizeof(int16_t));
    s_take_frames = frames;
    sampler_overview(data, frames, s_app.take_overview, SAMPLER_OVERVIEW);
    s_take_secs = s_app.mic_secs = (float)frames / SAMPLER_RATE;
}

static void load_boing(void) {
    static int16_t boing[SAMPLER_RATE];
    int n = sampler_boing(boing, SAMPLER_RATE);
    engine_load_sample(boing, n, SAMPLER_RATE, &s_app.sample_edit);
    show_take(boing, n);
    s_take_user = false;
}

// The MIC sound plays s_take_buf through its edit (START..END, CHOP).
static void reload_take(void) {
    sampler_edit_clamp(&s_app.sample_edit);
    engine_load_sample(s_take_buf, s_take_frames, SAMPLER_RATE, &s_app.sample_edit);
}

// A new take for the MIC sound (recorded, grabbed from tape or loaded from
// the library). It plays whole; CHOP stays as it was, so a bar grabbed
// into a chopped sampler is ready to play as slices.
static bool use_take(const int16_t *data, int frames) {
    if (frames <= 0) return false;
    if (frames > SAMPLER_MAX_FRAMES) frames = SAMPLER_MAX_FRAMES;
    if (data != s_take_buf) memmove(s_take_buf, data, (size_t)frames * sizeof(int16_t));
    s_app.sample_edit.start = 0.0f;
    s_app.sample_edit.end = 1.0f;
    if (s_app.sample_edit.slices) sampler_auto_chop(s_take_buf, frames, &s_app.sample_edit);  // re-cut at its hits
    if (!engine_load_sample(s_take_buf, frames, SAMPLER_RATE, &s_app.sample_edit)) return false;
    show_take(s_take_buf, frames);
    s_take_user = true;
    s_mic_gen++;
#ifndef VM_DEMO
    wav_write(STORAGE_DIR "/mic.wav", s_take_buf, frames, SAMPLER_RATE);
#endif
    return true;
}

static void finish_recording(void) {
    int frames = 0;
    int16_t *take = mic_stop(&frames);
    s_app.mic_rec = false;
    frames = sampler_tidy(take, frames);
    if (frames > SAMPLER_RATE / 20 && use_take(take, frames)) {
        ui_key_struck(0, PLAY_NOTE);
    } else {
        s_app.mic_secs = s_take_secs;  // silent or too short: keep the old take
    }
}

// SYNTH dice: random macros for a custom sound, a random patch in a bank.
static void roll_sound(void) {
    uint32_t r = (uint32_t)svcGetSystemTick() * 2654435761u + s_app.frame;
    const Sound *snd = sound_get(s_app.sound);
    if (snd->kind == SOUND_BANK) {
        change_patch(snd->bank_first + (int)((r >> 8) % (uint32_t)snd->bank_count));
    } else {
        set_macro(0, ((r >> 8) & 0xFF) / 255.0f);
        set_macro(1, ((r >> 16) & 0xFF) / 255.0f);
    }
    ui_key_struck((int)((r >> 4) % MUSIC_KEYS), PLAY_NOTE);
}

// ------------------------------------------------------------ save file ----

// Per-track tape version (and MIC version) when the current song was last
// loaded or saved. SAVE over that same song rewrites only what changed, so
// a file the load couldn't read is never replaced with nothing.
static uint32_t s_song_track_gen[TAPE_TRACKS];
static uint32_t s_song_mic_gen;

static void capture(SaveState *st) {
    memset(st, 0, sizeof(*st));  // zero padding too, so states compare with memcmp
    const SeqStatus seq = seq_status();  // live, not last frame's copy
    memcpy(st->name, s_app.song_name, sizeof(st->name));
    st->theme = s_theme;
    sync_part_out();
    for (int k = 0; k < PARTS; k++) {
        st->parts[k].sound = s_app.parts[k].sound;
        st->parts[k].patch = s_app.parts[k].patch;
        memcpy(st->parts[k].macro, s_app.parts[k].macro, sizeof(st->parts[k].macro));
        st->parts[k].pe = s_app.parts[k].pe;
        st->parts[k].wave = s_app.parts[k].wave;
    }
    st->has_edit = true;
    st->kit = s_app.kit;
    st->part = s_app.part;
    st->muted = seq.muted;
    memcpy(st->mix, s_app.mix, sizeof(st->mix));
    st->solo = s_app.mix_solo;
    const TapeStatus tp = tape_status();
    for (int t = 0; t < TAPE_TRACKS; t++) st->tape_source[t] = (int8_t)tp.source[t];
    st->sample_edit = s_app.sample_edit;
    st->key_root = (uint8_t)s_app.key_root;
    st->key_scale = (uint8_t)s_app.key_scale;
    st->arp = s_app.arp;
    seq_get_arrangement(&st->arr);
    for (int t = 0; t < TAPE_TRACKS; t++)
        if (tp.muted[t]) st->tape_muted |= (uint8_t)(1u << t);
    memcpy(st->params, s_app.params, sizeof(st->params));
    st->octave = s_app.octave;
    st->bpm = seq.bpm;
    for (int i = 0; i < SEQ_SLOTS; i++) seq_get_slot(i, &st->slots[i]);
    st->slot = seq.slot;
    st->song = seq.song;
    st->swing = seq.swing;
    memcpy(st->fx_x, s_app.fx_x, sizeof(st->fx_x));
    memcpy(st->fx_y, s_app.fx_y, sizeof(st->fx_y));
    for (int t = 0; t < TAPE_TRACKS; t++)
        if (tape_track_generation(t) != s_song_track_gen[t]) st->tape_changed |= (uint8_t)(1u << t);
    st->mic_changed = s_mic_gen != s_song_mic_gen;
}

static void restore(const SaveState *st) {
    s_app.kit = st->kit;
    apply_kit();
    s_theme = ((st->theme % ui_theme_count()) + ui_theme_count()) % ui_theme_count();
    ui_set_theme(s_theme);
    for (int i = 0; i < PARAM_COUNT; i++) s_app.params[i] = st->params[i];
    s_app.sample_edit = st->sample_edit;
    set_key(st->key_root, st->key_scale);
    set_arp(&st->arp);
    for (int k = PARTS - 1; k >= 0; k--) {
        float macro[SOUND_MACROS];
        memcpy(macro, st->parts[k].macro, sizeof(macro));
        if (st->old_mic_macros && st->parts[k].sound == sound_mic_index()) macro[0] = 1.0f;  // START became DECAY
        if (st->parts[k].sound >= 0)
            load_part(k, st->parts[k].sound, st->parts[k].patch, macro, st->has_edit ? &st->parts[k].pe : NULL,
                      st->has_edit ? &st->parts[k].wave : NULL);
        else  // an older song: this part gets its default sound
            load_part(k, kPartDefault[k], -1, NULL, NULL, NULL);
        upload_wave(k);
        seq_set_part_mute(k, (st->muted >> k) & 1u);
    }
    switch_part(st->part);
    seq_set_part_mute(MIX_DRUMS, (st->muted >> MIX_DRUMS) & 1u);
    memcpy(s_app.mix, st->mix, sizeof(s_app.mix));
    s_app.mix_solo = st->solo;
    mix_apply_all();
    for (int t = 0; t < TAPE_TRACKS; t++) {
        tape_set_source(t, st->tape_source[t]);
        tape_mute(t, (st->tape_muted >> t) & 1u);
    }
    seq_set_arrangement(&st->arr);
    s_app.arr_sel = -1;
    s_app.octave = st->octave < MUSIC_OCTAVE_MIN || st->octave > MUSIC_OCTAVE_MAX ? 0 : st->octave;
    seq_set_bpm(st->bpm);
    for (int i = 0; i < SEQ_SLOTS; i++) seq_put_slot(i, &st->slots[i]);
    seq_select_slot(st->slot);
    seq_set_song(st->song);
    seq_set_swing(st->swing);
    memcpy(s_app.fx_x, st->fx_x, sizeof(s_app.fx_x));
    memcpy(s_app.fx_y, st->fx_y, sizeof(s_app.fx_y));
    memcpy(s_app.song_name, st->name, sizeof(s_app.song_name));
    s_app.song_name[SONG_NAME_MAX] = '\0';
    if (s_app.song_name[0]) {  // older builds could store a trailing space
        char clean[SONG_NAME_MAX + 1];
        song_clean_name(s_app.song_name, clean);
        memcpy(s_app.song_name, clean, sizeof(clean));
    }
}

// Save a moment after things stop changing (and again on quit). The file
// is tiny, so this is cheap even on the 3DS's SD card.
#define SAVE_SETTLE_FRAMES 90
static SaveState s_saved, s_seen;
static unsigned s_changed_at;

static void autosave(bool force) {
    SaveState now;
    capture(&now);
    now.unsaved = s_app.song_dirty;  // so "unsaved" survives a restart
    if (memcmp(&now, &s_seen, sizeof(now)) != 0) {
        s_seen = now;
        s_changed_at = s_app.frame;
    }
    bool dirty = memcmp(&now, &s_saved, sizeof(now)) != 0;
    if (dirty && (force || s_app.frame - s_changed_at >= SAVE_SETTLE_FRAMES)) {
        if (storage_save(STORAGE_PATH, &now)) s_saved = now;
    }
}

// --------------------------------------------------------------- songs ----

// What the current song looked like when it was last saved or loaded.
static SaveState s_song_base;
static uint32_t s_song_tape_gen;
// Set at startup when the autosave says the song had unsaved changes: they
// are in the autosave, not in the song's folder, so it stays "unsaved".
static bool s_force_dirty;


static void notice(const char *msg) {
    snprintf(s_app.notice, sizeof(s_app.notice), "%s", msg);
    s_app.notice_frames = 150;
    s_app.notice_age = 0;
}

// --------------------------------------------------------- MIXDOWN ----
// The whole song, as it plays, into a stereo WAV: whatever you hear (live
// playing, tape, FX moves) goes in. It starts the song from the top and
// stops a few seconds after the sequencer does (a STOP arrangement ending,
// START, or tapping MIXDOWN), so echoes and reverb ring out.

#define MIXES_ROOT STORAGE_DIR "/mixes"
#define MIXDOWN_TAIL_FRAMES 180  // 3 s of tail
static WavStream s_mixdown;
static unsigned s_mixdown_tail_end;

#ifdef VM_RECORD
// The story recording itself (make STORY=1 RECORD=1): both screens every
// 4th frame as raw framebuffers, and the sound, for tools/make-video.sh.
#define REC_DIR STORAGE_DIR "/record"
#define REC_EVERY 4
static FILE *s_rec_top, *s_rec_bot, *s_rec_times;
static WavStream s_rec_audio;
static bool s_rec_on;
#endif

// Everything the audio thread captured since last time: to the mixdown,
// and (recording the story) to its soundtrack.
static void mixdown_drain(void) {
    static int16_t buf[4096 * 2];
    int n;
    while ((n = audio_out_capture_read(buf, 4096)) > 0) {
        if (s_app.mixdown) wav_stream_write(&s_mixdown, buf, n);
#ifdef VM_RECORD
        if (s_rec_on) wav_stream_write(&s_rec_audio, buf, n);
#endif
    }
    if (s_app.mixdown) s_app.mixdown_secs = s_mixdown.frames / (float)engine_sample_rate();
}

static void mixdown_finish(void) {
    mixdown_drain();
#ifdef VM_RECORD
    if (!s_rec_on)
#endif
        audio_out_capture(false);
    const uint32_t dropped = audio_out_capture_dropped();
    const bool ok = wav_stream_close(&s_mixdown);
    s_app.mixdown = 0;
    char msg[sizeof(s_app.notice)];
    const int s = (int)s_app.mixdown_secs;
    if (!ok)
        snprintf(msg, sizeof(msg), "MIXDOWN FAILED (SD CARD?)");
    else if (dropped)
        snprintf(msg, sizeof(msg), "MIX SAVED %d:%02d, WITH GAPS", s / 60, s % 60);
    else
        snprintf(msg, sizeof(msg), "MIX SAVED: %d:%02d IN MIXES", s / 60, s % 60);
    notice(msg);
}

static void stop_transports(void);

static void mixdown_start(void) {
    mixdown_drain();  // (recording the story: keep its soundtrack whole)
    char path[160];
    mkdir(MIXES_ROOT, 0777);
    snprintf(path, sizeof(path), "%s/%s.wav", MIXES_ROOT, s_app.song_name[0] ? s_app.song_name : "UNTITLED");
    stop_transports();
    if (!wav_stream_open(&s_mixdown, path, engine_sample_rate(), 2)) {
        notice("CAN'T WRITE THE MIX");
        return;
    }
    audio_out_capture(true);
    start_pressed();  // the song from the top (and a linked tape with it)
    s_app.mixdown = 1;
    s_app.mixdown_secs = 0.0f;
}

static void mixdown_button(void) {
    if (s_app.mixdown == 0)
        mixdown_start();
    else if (s_app.mixdown == 1)
        stop_transports();  // the tail follows
    else
        mixdown_finish();
}

// Once a frame.
static void mixdown_tick(void) {
#ifdef VM_RECORD
    if (s_rec_on) mixdown_drain();
#endif
    if (!s_app.mixdown) return;
    mixdown_drain();
    if (s_app.mixdown == 1 && !seq_status().playing) {  // the song is over: let it ring out
        s_app.mixdown = 2;
        s_mixdown_tail_end = s_app.frame + MIXDOWN_TAIL_FRAMES;
    }
    if (s_app.mixdown == 2 && (int)(s_app.frame - s_mixdown_tail_end) >= 0) mixdown_finish();
}

// ------------------------------------------------------- SONG sheet ----

static void arr_assign(int slot) {
    SeqArrangement a;
    seq_get_arrangement(&a);
    if (s_app.arr_sel < 0 || s_app.arr_sel >= a.count) return;
    a.rows[s_app.arr_sel].slot = (uint8_t)slot;
    seq_set_arrangement(&a);
}

static void arr_touch(int x, int y) {
    s_touch = TOUCH_IGNORE;
    SeqArrangement a;
    seq_get_arrangement(&a);
    const int hit = ui_hit_arr(x, y), sel = s_app.arr_sel;
    const bool sel_ok = sel >= 0 && sel < a.count;
    if (hit == ARR_HIT_END) {
        a.loop = !a.loop;
    } else if (hit == ARR_HIT_DEL && sel_ok) {
        memmove(&a.rows[sel], &a.rows[sel + 1], sizeof(SeqArrRow) * (size_t)(a.count - sel - 1));
        a.count--;
        if (s_app.arr_sel >= a.count) s_app.arr_sel = a.count - 1;
    } else if (hit >= ARR_HIT_MUTE && hit < ARR_HIT_CELL && sel_ok) {
        a.rows[sel].mutes ^= (uint8_t)(1u << (hit - ARR_HIT_MUTE));
    } else if (hit >= ARR_HIT_CELL) {
        const int i = hit - ARR_HIT_CELL;
        if (i < a.count) {
            if (i == sel) {  // tap the picked row again: x1 -> x2 -> x4 -> x8
                uint8_t *r = &a.rows[i].reps;
                *r = *r >= SEQ_ARR_REPS_MAX ? 1 : *r * 2;
            } else {
                s_app.arr_sel = i;
                return;
            }
        } else if (i == a.count && a.count < SEQ_ARR_ROWS) {  // "+": a row of the current slot
            a.rows[a.count++] = (SeqArrRow){(uint8_t)s_app.seq.slot, 1, 0};
            s_app.arr_sel = a.count - 1;
        } else {
            s_app.arr_sel = -1;
            return;
        }
    } else {
        return;
    }
    seq_set_arrangement(&a);
}

static void strip_name(int strip, char *out, size_t len) {
    if (strip == MIX_DRUMS)
        snprintf(out, len, "DRUMS");
    else
        snprintf(out, len, "PART %c", '1' + strip);
}

// Once a frame. A take of one strip is that strip "printed" to tape: when
// it ends, the live strip is muted so you hear it once, from the tape (unmute
// it on MIX to hear both, or to record it again). And a take of a muted
// strip would be silence, so say so when one starts.
static void watch_takes(void) {
    static bool was_recording;
    static int track;
    const bool rec = s_app.tape.recording;
    if (rec && !was_recording) {
        track = s_app.tape.track;
        const int src = s_app.tape.source[track];
        if (src >= 0 && !mix_audible(src)) {
            char name[8], msg[sizeof(s_app.notice)];
            strip_name(src, name, sizeof(name));
            snprintf(msg, sizeof(msg), "%s IS MUTED: UNMUTE ON MIX", name);
            notice(msg);
        }
    } else if (!rec && was_recording) {
        const int src = s_app.tape.source[track];
        if (src >= 0 && mix_audible(src)) {
            seq_set_part_mute(src, true);
            char name[8], msg[sizeof(s_app.notice)];
            strip_name(src, name, sizeof(name));
            snprintf(msg, sizeof(msg), "%s ON TAPE %c: LIVE ONE MUTED", name, '1' + track);
            notice(msg);
        }
    }
    was_recording = rec;
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void song_mark_clean(void) {
    for (int t = 0; t < TAPE_TRACKS; t++) s_song_track_gen[t] = tape_track_generation(t);
    s_song_tape_gen = tape_generation();
    s_song_mic_gen = s_mic_gen;
    s_song_kit_gen = s_kit_gen;
    capture(&s_song_base);
    s_force_dirty = false;
    s_app.song_dirty = false;
}

static void update_song_dirty(void) {
    SaveState now;
    capture(&now);
    s_app.song_dirty = s_force_dirty || memcmp(&now, &s_song_base, sizeof(now)) != 0 ||
                       tape_generation() != s_song_tape_gen || s_mic_gen != s_song_mic_gen || s_kit_gen != s_song_kit_gen;
}

// The autosaved MIC take should always be the current song's.
static void sync_scratch_mic(void) {
#ifndef VM_DEMO
    if (s_take_user)
        wav_write(STORAGE_DIR "/mic.wav", s_take_buf, s_take_frames, SAMPLER_RATE);
    else
        remove(STORAGE_DIR "/mic.wav");
#endif
}

// The sample library (the SONGS page's other mode): the files behind the
// names in s_app.songs.
#define SAMPLES_ROOT STORAGE_DIR "/samples"
#define PRESETS_ROOT STORAGE_DIR "/presets"
#define PRESET_EXT ".vmp"
static SampleFile s_sample_files[SONGS_MAX];
static const char *lib_root(void) { return s_app.lib_presets ? PRESETS_ROOT : SAMPLES_ROOT; }

static void refresh_songs(void) {
    if (s_app.lib_samples) {
        s_app.song_count = s_app.lib_presets ? sample_list_ext(PRESETS_ROOT, PRESET_EXT, s_sample_files, SONGS_MAX)
                                             : sample_list(SAMPLES_ROOT, s_sample_files, SONGS_MAX);
        for (int i = 0; i < s_app.song_count; i++) {
            memcpy(s_app.songs[i].name, s_sample_files[i].name, sizeof(s_app.songs[i].name));
            s_app.songs[i].bpm = 0;  // nothing more to read for a sample
            s_app.songs[i].patterns = 0;
        }
    } else {
        s_app.song_count = song_list(SONGS_ROOT, s_app.songs, SONGS_MAX, false);  // names only: fast
    }
    const int keep = s_app.song_sel;
    s_app.song_sel = -1;
    for (int i = 0; i < s_app.song_count && !s_app.lib_samples; i++)
        if (strcmp(s_app.songs[i].name, s_app.song_name) == 0) s_app.song_sel = i;
    if (s_app.lib_samples && keep >= 0 && keep < s_app.song_count) s_app.song_sel = keep;
    if (s_app.song_sel < 0 && s_app.song_count > 0) s_app.song_sel = 0;
    int max_scroll = s_app.song_count > SONG_ROWS ? s_app.song_count - SONG_ROWS : 0;
    if (s_app.song_sel >= 0 && s_app.song_sel < s_app.song_scroll) s_app.song_scroll = s_app.song_sel;
    if (s_app.song_sel >= s_app.song_scroll + SONG_ROWS) s_app.song_scroll = s_app.song_sel - SONG_ROWS + 1;
    if (s_app.song_scroll > max_scroll) s_app.song_scroll = max_scroll;
}

static void stop_transports(void) {
    if (seq_status().playing) seq_toggle_play();
    tape_play(false);
    release_all();
}

static bool save_song(const char *name) {
    char clean[SONG_NAME_MAX + 1], path[160];
    song_clean_name(name, clean);
    SaveState st;
    capture(&st);
    memcpy(st.name, clean, sizeof(st.name));  // the song takes this name only if the save works
    st.tape_changed = 0;
    st.mic_changed = false;
    // Saving over the song we loaded: its folder already holds every track
    // we haven't touched (including any the load couldn't read), so write
    // only what changed. Any other folder gets everything.
    const bool same = s_app.song_name[0] && strcmp(clean, s_app.song_name) == 0 && song_exists(SONGS_ROOT, clean);
    char dir[160];
    song_path(SONGS_ROOT, clean, NULL, dir, sizeof(dir));
    mkdir(SONGS_ROOT, 0777);
    mkdir(dir, 0777);
    bool ok = true;
    for (int t = 0; ok && t < TAPE_TRACKS; t++) {
        if (same && tape_track_generation(t) == s_song_track_gen[t]) continue;
        char file[16];
        snprintf(file, sizeof(file), "tape%d.wav", t + 1);
        song_path(SONGS_ROOT, clean, file, path, sizeof(path));
        int frames = 0;
        const int16_t *data = tape_track_data(t, &frames);
        if (data && frames > 0)
            ok &= wav_write(path, data, frames, TAPE_RATE);
        else
            remove(path);
    }
    song_path(SONGS_ROOT, clean, "mic.wav", path, sizeof(path));
    if (ok && !(same && s_mic_gen == s_song_mic_gen)) {
        if (s_take_user)
            ok &= wav_write(path, s_take_buf, s_take_frames, SAMPLER_RATE);
        else
            remove(path);
    }
    if (ok) ok &= save_lanes(dir);  // the drum lanes' own samples
    // song.vms goes last: if the audio failed, the old song.vms still
    // describes the old song.
    if (ok) ok = song_save_state(SONGS_ROOT, clean, &st);
    if (ok) {
        memcpy(s_app.song_name, clean, sizeof(clean));
        song_mark_clean();
    } else {
        notice("SAVE FAILED: SD CARD FULL?");
    }
    refresh_songs();
    return ok;
}

static bool load_song(const char *name) {
    SaveState st;
    if (!song_load_state(SONGS_ROOT, name, &st)) {
        notice("COULD NOT READ THAT SONG");
        return false;
    }
    int16_t *buf = malloc(TAPE_FRAMES * sizeof(int16_t));
    if (!buf) {  // nothing has changed yet: better to refuse than half-load
        notice("NOT ENOUGH MEMORY TO LOAD");
        return false;
    }
    stop_transports();
    restore(&st);
    memcpy(s_app.song_name, name, SONG_NAME_MAX + 1);
    char path[160];
    bool trouble = false;
    for (int t = 0; t < TAPE_TRACKS; t++) {
        char file[16];
        snprintf(file, sizeof(file), "tape%d.wav", t + 1);
        song_path(SONGS_ROOT, name, file, path, sizeof(path));
        int rate = 0, frames = wav_read(path, buf, TAPE_FRAMES, &rate);
        if (frames > 0 && rate == TAPE_RATE) {
            tape_load_track(t, buf, frames);
        } else {
            tape_clear(t);
            if (file_exists(path)) trouble = true;  // there but unreadable: SAVE won't touch it
        }
        tape_set_dirty(t);  // the autosaved copy should follow this song now
    }
    free(buf);
    song_path(SONGS_ROOT, name, "mic.wav", path, sizeof(path));
    int rate = 0, frames = wav_read(path, s_take_buf, SAMPLER_MAX_FRAMES, &rate);
    s_mic_gen++;
    if (frames > 0 && rate == SAMPLER_RATE && engine_load_sample(s_take_buf, frames, rate, &s_app.sample_edit)) {
        show_take(s_take_buf, frames);
        s_take_user = true;
    } else {
        load_boing();
        if (file_exists(path)) trouble = true;
    }
    {   // the drum lanes' own samples (restore() set the kit)
        char dir[160];
        song_path(SONGS_ROOT, name, NULL, dir, sizeof(dir));
        load_lanes(dir);
        save_lanes(STORAGE_DIR);
    }
    song_mark_clean();
    sync_scratch_mic();
    save_tape();  // the autosaved tapes follow the new song right away
    refresh_songs();
    if (trouble) notice("SOME FILES WOULDN'T LOAD");
    return true;
}

static void new_song(void) {
    stop_transports();
    seq_reset();
    seq_set_bpm(112);
    for (int t = 0; t < TAPE_TRACKS; t++) tape_clear(t);
    sampler_edit_default(&s_app.sample_edit);
    drum_kit_stock(&s_app.kit, 0);
    apply_kit();
    save_lanes(STORAGE_DIR);
    set_key(0, 0);
    set_arp(&(SeqArp){false, ARP_UP, ARP_16TH, 1});
    load_boing();
    s_mic_gen++;
    sync_scratch_mic();
    save_tape();
    const float initial[PARAM_COUNT] = {0.5f, 0.15f, 0.25f, 0.4f};
    for (int i = 0; i < PARAM_COUNT; i++) s_app.params[i] = initial[i];
    default_parts();
    mix_default();
    for (int t = 0; t < TAPE_TRACKS; t++) {
        tape_set_source(t, TAPE_SRC_ALL);
        tape_mute(t, false);
    }
    s_app.arr_sel = -1;
    s_app.arr_view = false;
    s_app.octave = 0;
    for (int k = 0; k < FX_COUNT; k++) s_app.fx_x[k] = s_app.fx_y[k] = 0.5f;
    s_app.song_name[0] = '\0';
    song_mark_clean();
    refresh_songs();
}

// Ask for a name with the 3DS keyboard, offering a random one.
#ifdef VM_STORY
static const char *s_story_name = "MY FIRST SONG";  // what the script "types" when asked for a name
#endif
static bool ask_name(char *out, const char *hint) {
    char suggestion[SONG_NAME_MAX + 1], buf[48];
    song_random_name((uint32_t)svcGetSystemTick(), suggestion);
#if defined(VM_STORY)
    (void)buf;
    (void)hint;
    snprintf(out, SONG_NAME_MAX + 1, "%s", s_story_name);
    return true;
#elif defined(VM_DEMO)
    (void)buf;
    strcpy(out, suggestion);
    return true;
#else
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, SONG_NAME_MAX);
    swkbdSetHintText(&kb, hint);
    swkbdSetInitialText(&kb, suggestion);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (swkbdInputText(&kb, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM) return false;
    song_clean_name(buf, out);
    return true;
#endif
}

// Slow jobs (writing ~10 MB of tape) run a couple of frames after the
// "SAVING..." card has been drawn, so the screen says what is happening.
typedef enum { JOB_NONE, JOB_SAVE, JOB_LOAD, JOB_NEW } Job;
static Job s_job;
static char s_job_name[SONG_NAME_MAX + 1];
static int s_job_wait;

static void start_job(Job job, const char *name, const char *msg) {
    if (s_job != JOB_NONE) return;  // one at a time: never drop a pending save
    // Stop the tape now so it is still by the time the save reads it.
    if (job == JOB_SAVE) tape_play(false);
    s_job = job;
    snprintf(s_job_name, sizeof(s_job_name), "%s", name ? name : "");
    s_app.busy = msg;
    s_job_wait = 2;
}

static void run_job(void) {
    if (s_job == JOB_NONE || --s_job_wait > 0) return;
    switch (s_job) {
        case JOB_SAVE: save_song(s_job_name); break;
        case JOB_LOAD: load_song(s_job_name); break;
        case JOB_NEW: new_song(); break;
        default: break;
    }
    s_job = JOB_NONE;
    s_app.busy = NULL;
}

static unsigned s_song_hold_frames;
static bool s_restored_unsaved, s_restored_mic_changed;
static uint8_t s_restored_tape_changed;

// ------------------------------------------------------ sample library ----

static void open_library(bool samples) {
    s_app.lib_samples = samples;
    s_app.lib_presets = false;
    s_app.page = PAGE_SONGS;
    s_app.confirm_name[0] = '\0';
    if (samples) {
        s_app.song_sel = -1;
        s_app.song_scroll = 0;
    }
    refresh_songs();
}

static bool sample_named(const char *name) {
    for (int i = 0; i < s_app.song_count; i++)
        if (strcmp(s_sample_files[i].name, name) == 0) return true;
    return false;
}

// The take's START..END region, as you hear it (CHOP is a way of playing
// it, not part of the file).
static void save_sample(const char *name) {
    if (!s_take_user) {
        notice("RECORD OR GRAB A SAMPLE FIRST");
        return;
    }
    mkdir(SAMPLES_ROOT, 0777);
    int first, n = sampler_region(&s_app.sample_edit, s_take_frames, -1, &first);
    char file[64], path[160];
    sample_file_for(name, file, sizeof(file));
    sample_path(SAMPLES_ROOT, file, path, sizeof(path));
    if (!wav_write(path, s_take_buf + first, n, SAMPLER_RATE)) {
        notice("SAVE FAILED");
        return;
    }
    refresh_songs();
    for (int i = 0; i < s_app.song_count; i++)
        if (strcmp(s_sample_files[i].name, name) == 0) s_app.song_sel = i;
    notice("SAMPLE SAVED");
}

// PRESETS: the part's sound as it is now (patch, macros, EDIT, DRAW wave).
static void save_preset(const char *name) {
    mkdir(PRESETS_ROOT, 0777);
    Preset p;
    memset(&p, 0, sizeof(p));
    p.sound = s_app.sound;
    p.patch = s_app.patch;
    memcpy(p.macro, s_app.macro, sizeof(p.macro));
    p.edit = part_edit()->cur;
    p.wave = s_app.parts[s_app.part].wave;
    char file[64], path[160];
    sample_file_for_ext(name, PRESET_EXT, file, sizeof(file));
    sample_path(PRESETS_ROOT, file, path, sizeof(path));
    if (!preset_save(path, &p)) {
        notice("SAVE FAILED");
        return;
    }
    refresh_songs();
    for (int i = 0; i < s_app.song_count; i++)
        if (strcmp(s_sample_files[i].name, name) == 0) s_app.song_sel = i;
    notice("SOUND SAVED");
}

static void load_preset_file(int i) {
    char path[160];
    sample_path(PRESETS_ROOT, s_sample_files[i].file, path, sizeof(path));
    Preset p;
    if (!preset_load(path, &p) || p.sound < 0 || p.sound >= sound_count()) {
        notice("CAN'T READ THAT PRESET");
        return;
    }
    PartEdit pe = *part_edit();
    pe.cur = p.edit;
    load_part(s_app.part, p.sound, p.patch, p.macro, &pe, &p.wave);
    char msg[sizeof(s_app.notice)];
    snprintf(msg, sizeof(msg), "%s ON PART %d", s_sample_files[i].name, s_app.part + 1);
    notice(msg);
}

static void open_presets(void) {
    open_library(true);
    s_app.lib_presets = true;
    refresh_songs();
}

// Any 16-bit WAV: resampled to the sampler's rate, the first few seconds.
static void load_sample(int i) {
    static int16_t raw[SAMPLER_SECONDS * 48000], res[SAMPLER_MAX_FRAMES];
    char path[160];
    sample_path(SAMPLES_ROOT, s_sample_files[i].file, path, sizeof(path));
    int rate = 0, frames = wav_read(path, raw, (int)(sizeof(raw) / sizeof(raw[0])), &rate);
    int n = frames > 0 ? sampler_resample(raw, frames, rate, res, SAMPLER_MAX_FRAMES, SAMPLER_RATE) : 0;
    if (n < SAMPLER_RATE / 50 || !use_take(res, n)) {
        notice("CAN'T READ THAT WAV");
        return;
    }
    notice("LOADED: PLAY IT AS THE MIC SOUND");
}

static void samples_touch(int x, int y) {
    s_touch = TOUCH_IGNORE;
    const bool confirming = s_app.confirm_name[0] != '\0';
    const int hit = ui_hit_songs(x, y, confirming);
    if (confirming) {
        if (hit == SONG_HIT_CONFIRM) {
            s_app.song_hold = SONG_HIT_CONFIRM;
            s_song_hold_frames = 0;
            s_touch = TOUCH_SONG_HOLD;
        } else {
            s_app.confirm_name[0] = '\0';
        }
        return;
    }
    const bool has_sel = s_app.song_sel >= 0 && s_app.song_sel < s_app.song_count;
    char name[SONG_NAME_MAX + 1];
    if (hit >= SONG_HIT_ROW) {
        int i = s_app.song_scroll + (hit - SONG_HIT_ROW);
        if (i < s_app.song_count) s_app.song_sel = i;
        return;
    }
    switch (hit) {
        case SONG_HIT_UP:
            if (s_app.song_scroll > 0) s_app.song_scroll--;
            break;
        case SONG_HIT_DOWN:
            if (s_app.song_scroll + SONG_ROWS < s_app.song_count) s_app.song_scroll++;
            break;
        case SONG_HIT_SAVE_AS:
            if (!ask_name(name, s_app.lib_presets ? "Name this sound" : "Name your sample")) break;
            if (sample_named(name))
                snprintf(s_app.confirm_name, sizeof(s_app.confirm_name), "%s", name);
            else if (s_app.lib_presets)
                save_preset(name);
            else
                save_sample(name);
            break;
        case SONG_HIT_LOAD:
            if (has_sel) s_app.lib_presets ? load_preset_file(s_app.song_sel) : load_sample(s_app.song_sel);
            break;
        case SONG_HIT_DELETE:
            if (!has_sel) break;
            s_app.song_hold = hit;
            s_song_hold_frames = 0;
            s_touch = TOUCH_SONG_HOLD;
            break;
        case SONG_HIT_NEW:  // BACK, in this mode
            s_app.page = PAGE_SYNTH;
            break;
        default: break;
    }
}

static void song_hold_done(int button) {
    if (s_app.lib_samples) {
        if (button == SONG_HIT_DELETE && s_app.song_sel >= 0 && s_app.song_sel < s_app.song_count) {
            char path[160];
            sample_path(lib_root(), s_sample_files[s_app.song_sel].file, path, sizeof(path));
            if (s_app.lib_presets)
                storage_remove(path);  // and the copies a safe save keeps
            else
                remove(path);
            refresh_songs();
        }
        if (button == SONG_HIT_CONFIRM) {
            s_app.lib_presets ? save_preset(s_app.confirm_name) : save_sample(s_app.confirm_name);
            s_app.confirm_name[0] = '\0';
        }
        return;
    }
    const bool has_sel = s_app.song_sel >= 0 && s_app.song_sel < s_app.song_count;
    if (button == SONG_HIT_LOAD && has_sel) start_job(JOB_LOAD, s_app.songs[s_app.song_sel].name, "LOADING...");
    if (button == SONG_HIT_DELETE && has_sel) {
        if (strcmp(s_app.songs[s_app.song_sel].name, s_app.song_name) == 0) s_app.song_name[0] = '\0';
        song_delete(SONGS_ROOT, s_app.songs[s_app.song_sel].name);
        refresh_songs();
    }
    if (button == SONG_HIT_NEW) start_job(JOB_NEW, NULL, "NEW SONG...");
    if (button == SONG_HIT_CONFIRM) {
        start_job(JOB_SAVE, s_app.confirm_name, "SAVING...");
        s_app.confirm_name[0] = '\0';
    }
}

// SAVE AS with a name: ask first if it would replace a different song.
static void save_as(const char *name) {
    if (song_exists(SONGS_ROOT, name) && strcmp(name, s_app.song_name) != 0)
        snprintf(s_app.confirm_name, sizeof(s_app.confirm_name), "%s", name);
    else
        start_job(JOB_SAVE, name, "SAVING...");
}

static void songs_touch(int x, int y) {
    if (s_app.lib_samples) {
        samples_touch(x, y);
        return;
    }
    s_touch = TOUCH_IGNORE;
    if (s_job != JOB_NONE) return;  // busy: ignore taps until it's done
    const bool confirming = s_app.confirm_name[0] != '\0';
    const int hit = ui_hit_songs(x, y, confirming);
    if (confirming) {
        if (hit == SONG_HIT_CONFIRM) {
            s_app.song_hold = SONG_HIT_CONFIRM;
            s_song_hold_frames = 0;
            s_touch = TOUCH_SONG_HOLD;
        } else {
            s_app.confirm_name[0] = '\0';  // tapped elsewhere: cancelled
        }
        return;
    }
    const bool has_sel = s_app.song_sel >= 0 && s_app.song_sel < s_app.song_count;
    char name[SONG_NAME_MAX + 1];
    if (hit >= SONG_HIT_ROW) {
        int i = s_app.song_scroll + (hit - SONG_HIT_ROW);
        if (i < s_app.song_count) s_app.song_sel = i;
        return;
    }
    switch (hit) {
        case SONG_HIT_UP:
            if (s_app.song_scroll > 0) s_app.song_scroll--;
            break;
        case SONG_HIT_DOWN:
            if (s_app.song_scroll + SONG_ROWS < s_app.song_count) s_app.song_scroll++;
            break;
        case SONG_HIT_SAVE_AS:
            if (ask_name(name, "Name your song")) save_as(name);
            break;
        case SONG_HIT_SAVE:
            if (s_app.song_name[0])
                start_job(JOB_SAVE, s_app.song_name, "SAVING...");
            else if (ask_name(name, "Name your song"))
                save_as(name);
            break;
        case SONG_HIT_LOAD:
            if (!has_sel) break;
            if (!s_app.song_dirty) {
                start_job(JOB_LOAD, s_app.songs[s_app.song_sel].name, "LOADING...");
                break;
            }
            /* fall through: unsaved changes, so LOAD is held */
        case SONG_HIT_DELETE:
        case SONG_HIT_NEW:
            if (hit != SONG_HIT_NEW && !has_sel) break;
            s_app.song_hold = hit;
            s_song_hold_frames = 0;
            s_touch = TOUCH_SONG_HOLD;
            break;
        default: break;
    }
}

static void fx_pad_move(float x, float y) {
    int k = s_app.fx_sel;
    s_app.fx_x[k] = x;
    s_app.fx_y[k] = y;
    fx_set((FxKind)k, true, x, y);
}

static void fx_pad_release(void) {
    int k = s_app.fx_sel;
    if (s_app.fx_hold) {
        s_app.fx_latched[k] = true;  // HOLD keeps it running
    } else {
        fx_set((FxKind)k, false, s_app.fx_x[k], s_app.fx_y[k]);
    }
}

// FX automation. Recording (the sequencer's ● on, playing): wherever the pad
// is held, the effect and its position go into the steps passing by.
// Playing: each step's recorded move is applied, and the effect it turned
// on is let go at the next step without one. Your stylus always wins.
static int s_auto_fx = -1;  // the FxKind automation turned on, or -1
static int s_auto_step = -1;

static void auto_release(void) {
    const int k = s_auto_fx;
    if (k >= 0 && !s_app.fx_latched[k] && !(s_touch == TOUCH_FX_PAD && s_app.fx_sel == k))
        fx_set((FxKind)k, false, s_app.fx_x[k], s_app.fx_y[k]);
    s_auto_fx = -1;
}

static void fx_automation(void) {
    const SeqStatus *st = &s_app.seq;
    if (!st->playing) {
        auto_release();
        s_auto_step = -1;
        return;
    }
    const bool held = s_touch == TOUCH_FX_PAD;
    const int step = st->step, k = s_app.fx_sel;
    if (st->recording && held) seq_set_fx(step, k + 1, s_app.fx_x[k], s_app.fx_y[k]);
    if (step == s_auto_step) return;
    s_auto_step = step;
    if (held) return;
    const SeqPattern *p = &s_app.pattern;
    if (p->fx[step]) {
        const int kk = p->fx[step] - 1;
        if (s_auto_fx >= 0 && s_auto_fx != kk) auto_release();
        fx_set((FxKind)kk, true, p->fx_x[step] / 255.0f, p->fx_y[step] / 255.0f);
        s_auto_fx = kk;
    } else {
        auto_release();
    }
}

static void fx_sticker(int hit) {
    if (hit == FX_HIT_HOLD) {
        s_app.fx_hold = !s_app.fx_hold;
        if (!s_app.fx_hold)  // releasing HOLD lets go of everything it held
            for (int k = 0; k < FX_COUNT; k++)
                if (s_app.fx_latched[k]) {
                    s_app.fx_latched[k] = false;
                    fx_set((FxKind)k, false, s_app.fx_x[k], s_app.fx_y[k]);
                }
        return;
    }
    if (hit == s_app.fx_sel && s_app.fx_latched[hit]) {  // tap a held effect again: off
        s_app.fx_latched[hit] = false;
        fx_set((FxKind)hit, false, s_app.fx_x[hit], s_app.fx_y[hit]);
    }
    s_app.fx_sel = hit;
}

static void set_param(int i, float v) {
    // Ignore sub-1% wobble from the resistive touchscreen.
    if (fabsf(v - s_app.params[i]) < 0.01f) return;
    s_app.params[i] = v;
    engine_set_param((EngineParam)i, v);
}

// ---------------------------------------------------------- sequencer ----

// The sequencer plays through the engine.
static void seq_out(int synth, int note, float velocity) {
    if (velocity > 0.0f)
        engine_note_on(synth, note, velocity);
    else
        engine_note_off(synth, note);
}

static unsigned s_slot_frames;
static unsigned s_part_hold_frames;

// --------------------------------------------------------------- roll ----

#define AUDITION_FRAMES 14
static int s_audition_pitch = -1, s_audition_frames, s_audition_synth = SYNTH_LEAD;

// Play a pitch briefly so you hear what you're placing.
static void audition(int pitch) {
    if (s_audition_pitch >= 0) engine_note_off(s_audition_synth, s_audition_pitch);
    s_audition_synth = part_synth(s_app.part);
    engine_note_on(s_audition_synth, pitch, 0.7f);
    s_audition_pitch = pitch;
    s_audition_frames = AUDITION_FRAMES;
}

// --------------------------------------------------- MIC sample editor ----

static void sampler_drag(int x) {
    SampleEdit *e = &s_app.sample_edit;
    const float min_len = 0.02f;
    if (s_app.smp_grab >= SMP_GRAB_CUT) {  // a cut moves between its neighbours
        const int k = s_app.smp_grab - SMP_GRAB_CUT;
        const float room = 0.006f, lo = sampler_bound(e, k) + room, hi = sampler_bound(e, k + 2) - room;
        const float v = ui_sampler_pos(x);
        if (lo < hi) e->cut[k] = v < lo ? lo : v > hi ? hi : v;
    } else if (s_app.smp_grab == 1) {
        float v = ui_sampler_pos(x);
        e->start = v > e->end - min_len ? e->end - min_len : v;
        sampler_edit_clamp(e);  // pushes cuts along
    } else if (s_app.smp_grab == 2) {
        float v = ui_sampler_pos(x);
        e->end = v < e->start + min_len ? e->start + min_len : v;
        sampler_edit_clamp(e);
    } else if (s_app.smp_grab >= 3) {
        set_macro(s_app.smp_grab - 3, ui_sampler_slider(x, s_app.smp_grab - 3));
    }
}

// MIC editor touches; false if (x, y) isn't on it.
static bool sampler_begin(int x, int y) {
    SampleEdit *e = &s_app.sample_edit;
    const int hit = ui_hit_sampler(x, y);
    if (hit == SMP_HIT_NONE) return false;
    s_touch = TOUCH_IGNORE;
    if (hit == SMP_HIT_CHOP) {  // off -> 4 -> 8 -> off, cut at the hits
        e->slices = e->slices == 0 ? 4 : e->slices == 4 ? 8 : 0;
        if (e->slices) {
            const int hits = sampler_auto_chop(s_take_buf, s_take_frames, e);
            char msg[sizeof(s_app.notice)];
            if (hits == e->slices - 1)
                snprintf(msg, sizeof(msg), "CHOP %d: CUT AT THE HITS", e->slices);
            else if (hits > 0)
                snprintf(msg, sizeof(msg), "CHOP %d: %d CUTS ON HITS", e->slices, hits);
            else
                snprintf(msg, sizeof(msg), "CHOP %d: EVEN SLICES", e->slices);
            notice(msg);
        }
        reload_take();
    } else if (hit == SMP_HIT_LIB) {
        open_library(true);
    } else if (hit == SMP_HIT_TOKIT) {
        if (e->slices) slices_to_kit();
    } else if (hit == SMP_HIT_WAVE) {
        const float pos = ui_sampler_pos(x), near = 8.0f / SMP_WAVE_W;
        // The nearest boundary: START (0), the cuts, END (slices).
        const int nb = e->slices ? e->slices : 1;
        int best = 0;
        float bd = 2.0f;
        for (int b = 0; b <= nb; b++) {
            const float d = fabsf(pos - sampler_bound(e, b)) + (b == 0 || b == nb ? 0.0f : 0.0005f);  // ties: START / END
            if (d < bd) bd = d, best = b;
        }
        if (y >= SMP_WAVE_Y + SMP_WAVE_H || bd < near) {  // a handle (or its grip)
            s_app.smp_grab = best == 0 ? 1 : best == nb ? 2 : SMP_GRAB_CUT + best - 1;
            s_touch = TOUCH_SMP;
        } else {  // tap the take: hear that slice, or the region
            int k = -1;
            for (int b = 0; e->slices && b < e->slices; b++)
                if (pos >= sampler_bound(e, b) && pos < sampler_bound(e, b + 1)) k = b;
            int pitch[MUSIC_MAX_NOTES];
            music_key_notes(k >= 0 ? k : 0, PLAY_NOTE, 0, pitch);  // the key that plays slice k
            audition(pitch[0]);
            s_app.smp_flash = k;
            s_app.smp_flash_frames = 12;
        }
    } else if (hit >= SMP_HIT_SLIDER) {
        s_app.smp_grab = 3 + (hit - SMP_HIT_SLIDER);
        sampler_drag(x);
        s_touch = TOUCH_SMP;
    }
    return true;
}

// TAPE's SAMPLE: the bar under the playhead on the picked track becomes the
// MIC sound's take. (Bars count from the tape's start, where SEQ LINK
// starts the beat.)
static void grab_from_tape(void) {
    const TapeStatus tp = tape_status();
    char msg[sizeof(s_app.notice)];
    if (tp.recording) {
        notice("STOP RECORDING FIRST");
        return;
    }
    int len = 0;
    const int16_t *data = tape_track_data(tp.track, &len);
    const float bar = 240.0f / (float)(s_app.seq.bpm > 0 ? s_app.seq.bpm : 120);
    const int bar_no = (int)(fmodf(tp.seconds, (float)TAPE_SECONDS) / bar);
    int a = (int)(bar_no * bar * TAPE_RATE), n = (int)((bar < SAMPLER_SECONDS ? bar : SAMPLER_SECONDS) * TAPE_RATE);
    if (!data || !tp.used[tp.track] || a >= len) {
        snprintf(msg, sizeof(msg), "NOTHING ON TAPE %c THERE", '1' + tp.track);
        notice(msg);
        return;
    }
    if (a + n > len) n = len - a;
    static int16_t buf[SAMPLER_MAX_FRAMES];
    int m = sampler_resample(data + a, n, TAPE_RATE, buf, SAMPLER_MAX_FRAMES, SAMPLER_RATE);
    sampler_normalise(buf, m);
    if (m < SAMPLER_RATE / 20 || !use_take(buf, m)) {
        notice("COULDN'T GRAB THAT");
        return;
    }
    snprintf(msg, sizeof(msg), "TAPE %c BAR %d -> MIC SOUND", '1' + tp.track, bar_no + 1 > 99 ? 99 : bar_no + 1);
    notice(msg);
}

static void audition_tick(void) {
    if (s_audition_pitch >= 0 && --s_audition_frames <= 0) {
        engine_note_off(s_audition_synth, s_audition_pitch);
        s_audition_pitch = -1;
    }
}

typedef enum { ROLL_MOVE, ROLL_STRETCH } RollDrag;
static RollDrag s_roll_mode;
static SeqRollNote s_roll_orig;  // the note as it was when the drag began
static int s_roll_anchor_step, s_roll_anchor_row, s_roll_last_step, s_roll_last_row;
static bool s_roll_moved, s_roll_created;
static int s_roll_len = 1;       // length new notes get (the last one you set)
static int s_keys_last_y;

static int roll_pitch(int row) { return music_roll_pitch(s_app.roll_scroll + row, s_app.roll_chromatic); }

static void roll_scroll_by(int rows) {
    int max = music_roll_rows(s_app.roll_chromatic) - ROLL_ROWS;
    int v = s_app.roll_scroll + rows;
    s_app.roll_scroll = v < 0 ? 0 : v > max ? max : v;
}

static void roll_begin(int x, int y) {
    int part = ui_hit_roll_part(x, y);
    if (part >= 0) {
        switch_part(part);
        s_touch = TOUCH_IGNORE;
        return;
    }
    if (ui_hit_roll_toggle(x, y)) {
        // Keep the same pitch at the bottom when switching scale / chromatic.
        int bottom = roll_pitch(0);
        s_app.roll_chromatic = !s_app.roll_chromatic;
        s_app.roll_scroll = music_roll_row(bottom, s_app.roll_chromatic);
        roll_scroll_by(0);
        s_touch = TOUCH_IGNORE;
        return;
    }
    if (ui_hit_roll_keys(x, y)) {
        int step, row;
        ui_roll_cell_clamped(ROLL_X0, y, &step, &row);
        audition(roll_pitch(row));
        s_keys_last_y = y;
        s_touch = TOUCH_ROLL_KEYS;
        return;
    }
    int step, row;
    if (!ui_roll_cell(x, y, &step, &row)) {
        s_touch = TOUCH_IGNORE;
        return;
    }
    step = page_step(step);
    const int pitch = roll_pitch(row);
    int idx = seq_roll_find(s_app.part, step, pitch);
    if (idx >= 0 && s_app.mode == PLAY_DRUM) {  // R + tap a note: SLIDE on / off
        SeqPattern pat;
        seq_snapshot(&pat);
        const int f = pat.notes[idx].flags;
        seq_roll_set_flags(idx, f ^ SEQ_NOTE_SLIDE);
        notice(f & SEQ_NOTE_SLIDE ? "SLIDE OFF" : "SLIDE: GLIDES ON (MONO + GLIDE)");
        s_touch = TOUCH_IGNORE;
        return;
    }
    s_roll_moved = s_roll_created = false;
    if (idx < 0) {  // empty: a new note, which a drag to the right stretches
        idx = seq_roll_add(s_app.part, step, pitch, s_roll_len, (int)(SEQ_VEL_SYNTH * 255));
        if (idx < 0) {
            s_touch = TOUCH_IGNORE;  // pattern full
            return;
        }
        s_roll_created = true;
        s_roll_mode = ROLL_STRETCH;
        audition(pitch);
    }
    SeqPattern pat;
    seq_snapshot(&pat);
    s_roll_orig = pat.notes[idx];
    if (!s_roll_created) {
        // Grab the last cell of a note to stretch it, anywhere else to move it.
        int last = s_roll_orig.step + s_roll_orig.len - 1;
        bool right_half = (x - ROLL_X0) % ROLL_CELL_W >= ROLL_CELL_W / 2;
        s_roll_mode = (step == last && (s_roll_orig.len > 1 || right_half)) ? ROLL_STRETCH : ROLL_MOVE;
    }
    s_app.roll_active = idx;
    s_roll_anchor_step = s_roll_last_step = step;
    s_roll_anchor_row = s_roll_last_row = row;
    s_touch = TOUCH_ROLL_NOTE;
}

static void roll_drag(int x, int y) {
    int step, row;
    ui_roll_cell_clamped(x, y, &step, &row);
    step = page_step(step);
    if (step == s_roll_last_step && row == s_roll_last_row) return;
    s_roll_last_step = step;
    s_roll_last_row = row;
    s_roll_moved = true;
    const int idx = s_app.roll_active;
    if (s_roll_mode == ROLL_STRETCH) {
        int len = step - s_roll_orig.step + 1;
        seq_roll_set(idx, s_roll_orig.step, s_roll_orig.pitch, len < 1 ? 1 : len);
        s_roll_len = len < 1 ? 1 : len;
        return;
    }
    int new_step = s_roll_orig.step + (step - s_roll_anchor_step);
    const int last = seq_len(&s_app.pattern) - 1;
    new_step = new_step < 0 ? 0 : new_step > last ? last : new_step;
    int rows = music_roll_rows(s_app.roll_chromatic);
    int abs_row = music_roll_row(s_roll_orig.pitch, s_app.roll_chromatic) + (row - s_roll_anchor_row);
    abs_row = abs_row < 0 ? 0 : abs_row >= rows ? rows - 1 : abs_row;
    int pitch = row == s_roll_anchor_row ? s_roll_orig.pitch : music_roll_pitch(abs_row, s_app.roll_chromatic);
    SeqPattern pat;
    seq_snapshot(&pat);
    if (idx >= 0 && idx < pat.count && pat.notes[idx].pitch != pitch) audition(pitch);
    seq_roll_set(idx, new_step, pitch, s_roll_orig.len);
}

static void roll_end(void) {
    // A tap on a note (no drag) removes it; with L held, it changes how hard
    // it plays: normal -> loud -> soft -> normal.
    if (!s_roll_moved && !s_roll_created && s_app.roll_active >= 0) {
        if (s_keys_held & KEY_L) {
            const int v = s_app.pattern.notes[s_app.roll_active].vel;
            seq_roll_set_vel(s_app.roll_active, v > 200 ? 90 : v < 120 ? (int)(SEQ_VEL_SYNTH * 255) : 255);
        } else {
            seq_roll_remove(s_app.roll_active);
        }
    }
    s_app.roll_active = -1;
}

static bool cell_on(int column, int row) {
    const int step = page_step(column);
    int tmp[4];
    if (row == 0) return seq_notes_at(&s_app.pattern, s_app.part, step, tmp, 4) > 0;
    if (row == SEQ_ROW_ACCENT) return (s_app.pattern.accents >> step) & 1ull;
    return (s_app.pattern.drums[step] >> (row - 1)) & 1u;
}

static void paint_cell(int column, int row) {
    const int step = page_step(column);
    if (row == 0)
        seq_set_synth(s_app.part, step, s_paint_on ? &s_last_note : NULL);
    else if (row == SEQ_ROW_ACCENT)
        seq_set_accent(step, s_paint_on);
    else
        seq_set_drum(step, row - 1, s_paint_on);
    s_paint_step = column;
}

// One level of undo for the big edits (✕, the dice, copying into a slot):
// the slot as it was before.
static SeqPattern s_undo;
static int s_undo_slot = -1;

static void undo_save(int slot) {
    seq_get_slot(slot, &s_undo);
    s_undo_slot = slot;
}

static void undo(void) {
    if (s_undo_slot < 0) {
        notice("NOTHING TO UNDO");
        return;
    }
    SeqPattern now;
    seq_get_slot(s_undo_slot, &now);
    seq_put_slot(s_undo_slot, &s_undo);
    s_undo = now;  // hold again to redo
    notice("UNDONE (HOLD X AGAIN TO REDO)");
}

// ✕: in the piano roll, only the current part's notes; on the grid, the
// whole pattern.
static void clear_pattern(void) {
    undo_save(s_app.seq.slot);
    if (s_app.roll_view) {
        SeqPattern p = s_app.pattern;
        p.count = 0;
        seq_set_part_pattern(s_app.part, &p);  // drums and accents stay as they are
    } else {
        seq_clear();
    }
}

// A one-bar pattern repeated out to `len` steps.
static void repeat_bar(SeqPattern *p, int len) {
    p->len = (uint8_t)len;
    const int n = p->count;
    for (int s = SEQ_STEPS; s < len; s++) {
        p->drums[s] = p->drums[s % SEQ_STEPS];
        if ((p->accents >> (s % SEQ_STEPS)) & 1ull) p->accents |= 1ull << s;
    }
    for (int start = SEQ_STEPS; start < len; start += SEQ_STEPS)
        for (int i = 0; i < n && p->count < SEQ_NOTES; i++) {
            p->notes[p->count] = p->notes[i];
            p->notes[p->count++].step = (uint8_t)(p->notes[i].step + start);
        }
}

static void press_button(SeqButton b) {
    switch (b) {
        case SEQ_BTN_PLAY: seq_toggle_play(); break;
        case SEQ_BTN_REC: seq_set_recording(!s_app.seq.recording); break;
        case SEQ_BTN_BPM_DOWN: seq_set_bpm(s_app.seq.bpm - 1); break;
        case SEQ_BTN_BPM_UP: seq_set_bpm(s_app.seq.bpm + 1); break;
        case SEQ_BTN_CLEAR: break;  // on release (a hold is undo)
        case SEQ_BTN_DICE: {
            undo_save(s_app.seq.slot);
            SeqPattern p;
            dice_groove(&p, (uint32_t)svcGetSystemTick() ^ (s_app.frame * 2654435761u), s_app.octave);
            repeat_bar(&p, seq_len(&s_app.pattern));  // a longer pattern gets it in every bar
            seq_set_part_pattern(s_app.part, &p);  // new drums, and new notes for this part only
            for (int k = 0; k < MUSIC_KEYS; k += 2) ui_seq_struck(k, k & 2);  // celebrate the roll
            break;
        }
        default: break;
    }
}

// Visual echo of the sequencer: bursts and key flashes for each step fired.
static void follow_sequencer(void) {
    if (s_app.seq.fired == s_seen_fired) return;
    s_seen_fired = s_app.seq.fired;
    if (!s_app.seq.playing) return;
    const int step = s_app.seq.step;
    // The keys light up for what they would play: the drums only on the
    // grid (whose drum lanes are the keys) or while R makes them drums;
    // elsewhere -- the piano roll, SYNTH... -- just the current part's notes.
    const bool keys_are_drums = s_app.mode == PLAY_DRUM || (s_app.page == PAGE_SEQ && !s_app.roll_view && !s_app.arr_view);
    const bool drums_muted = (s_app.seq.muted >> PARTS) & 1u;
    for (int lane = 0; lane < SEQ_LANES && !drums_muted; lane++)
        if ((s_app.pattern.drums[step] >> lane) & 1u) {
            ui_seq_struck(lane, true);
            if (keys_are_drums) s_flash[lane] = FLASH_FRAMES;
        }
    int pitches[8];
    // Every part's notes fire bursts; only the current part lights the keys.
    for (int part = 0; part < PARTS; part++) {
        if ((s_app.seq.muted >> part) & 1u) continue;
        int n = seq_notes_at(&s_app.pattern, part, step, pitches, 8);
        for (int i = 0; i < n && i < 8; i++) {
            int key = music_key_of_pitch(pitches[i]);
            if (key < 0) continue;
            ui_seq_struck(key, false);
            if (part == s_app.part && s_app.mode != PLAY_DRUM) s_flash[key] = FLASH_FRAMES;
        }
    }
}

// --------------------------------------------------------------- input ----

// --- the EDIT view -------------------------------------------------------

static float s_edit_tmp[ED_PARAMS];  // the envelope while a handle is dragged
static int s_edit_slider = -1;       // EditParam, or ED_PARAMS for MORPH
static int s_edit_slider_x0, s_edit_slider_w;
static bool s_draw_started;          // DRAW: s_draw_x / _y hold the last point drawn
static float s_draw_x, s_draw_y;
static void open_presets(void);

static void edit_env_from(const float *p) {
    SynthEdit *e = &part_edit()->cur;
    static const EditParam env[4] = {ED_ATTACK, ED_DECAY, ED_SUSTAIN, ED_RELEASE};
    for (int i = 0; i < 4; i++)
        if (fabsf(p[env[i]] - e->p[env[i]]) > 0.001f) edit_set(e, env[i], p[env[i]]);
    apply_edit();
}

static void edit_pad(int x, int y) {
    float nx, ny;
    ui_edit_pad_pos(x, y, &nx, &ny);
    SynthEdit *e = &part_edit()->cur;
    if (s_app.edit_page == EDP_FILTER) {
        edit_set(e, ED_CUTOFF, nx);
        edit_set(e, ED_RESO, ny);
    } else {
        edit_set(e, ED_LFO_RATE, nx);
        edit_set(e, ED_LFO_DEPTH, ny);
    }
    apply_edit();
}

// MORPH: the edit (and the two macros) t of the way from A to B.
static void edit_morph_to(float t) {
    PartEdit *pe = part_edit();
    pe->morph = t;
    if (pe->snaps != 3) return;
    edit_morph(&pe->snap[0], &pe->snap[1], t, &pe->cur);
    for (int m = 0; m < SOUND_MACROS; m++)
        s_app.macro[m] = pe->snap_macro[0][m] + (pe->snap_macro[1][m] - pe->snap_macro[0][m]) * t;
    if (sound_edit_key(s_app.sound, &pe->cur) != s_edit_key[s_app.part]) {
        apply_edit();  // reloads, macros included
    } else {
        for (int m = 0; m < SOUND_MACROS; m++) engine_set_macro(s_app.part, s_app.sound, m, s_app.macro[m]);
        engine_apply_edit(s_app.part, &pe->cur);
    }
}

static void edit_slider_move(int x) {
    const float v = ui_edit_slider(x, s_edit_slider_x0, s_edit_slider_w);
    if (s_edit_slider == ED_PARAMS) {
        edit_morph_to(v);
        return;
    }
    edit_set(&part_edit()->cur, (EditParam)s_edit_slider, v);
    apply_edit();
}

static void edit_grab_slider(int param, int x0, int w, int x) {
    s_edit_slider = param;
    s_edit_slider_x0 = x0;
    s_edit_slider_w = w;
    s_touch = TOUCH_EDIT_SLIDER;
    edit_slider_move(x);
}

static void edit_voice_touch(int x, int y) {
    PartEdit *pe = part_edit();
    SynthEdit *e = &pe->cur;
    const int row = (y - ED_ROW_Y0 + 2) / ED_ROW_PITCH;
    const bool chips = x < ED_SLIDER_X - 2;
    s_touch = TOUCH_IGNORE;
    switch (row) {
        case 0:
            if (chips) {
                e->mono = !e->mono;
                apply_edit();
                notice(e->mono ? "MONO: ONE NOTE AT A TIME, GLIDES" : "POLY: CHORDS AGAIN");
            } else {
                edit_grab_slider(ED_GLIDE, ED_SLIDER_X, ED_SLIDER_W, x);
            }
            break;
        case 1:
            if (chips && sound_unison_kind(s_app.sound) != 1) {
                notice(sound_unison_kind(s_app.sound) ? "BUILT IN: DETUNE SPREADS IT" : "NO UNISON ON THIS SOUND");
            } else if (chips) {
                int u = 1 + (x - ED_BODY_X) / (ED_CHIP_W + 3);
                e->unison = (uint8_t)(u < 1 ? 1 : u > UNISON_MAX ? UNISON_MAX : u);
                apply_edit();
                char msg[sizeof(s_app.notice)];
                const int v = sound_edit_voices(s_app.sound, e);
                snprintf(msg, sizeof(msg), "UNISON x%d: %d VOICE%s LEFT", e->unison, v, v == 1 ? "" : "S");
                notice(msg);
            } else {
                edit_grab_slider(ED_DETUNE, ED_SLIDER_X, ED_SLIDER_W, x);
            }
            break;
        case 2: edit_grab_slider(ED_BRIGHT, ED_BODY_X, ED_BODY_W, x); break;
        case 3:
            if (chips) {
                edit_mutate(e, 0.12f, (uint32_t)svcGetSystemTick() ^ (s_app.frame * 2654435761u));
                apply_edit();
                ui_key_struck((int)(s_app.frame % MUSIC_KEYS), PLAY_NOTE);
            }
            break;
        default:
            if (row != 4) break;
            if (chips) {  // A / B: store the sound as it is now
                const int sn = x < ED_BODY_X + 35 ? 0 : 1;
                pe->snap[sn] = pe->cur;
                memcpy(pe->snap_macro[sn], s_app.macro, sizeof(pe->snap_macro[sn]));
                pe->snaps |= (uint8_t)(1u << sn);
                pe->morph = (float)sn;
                notice(pe->snaps == 3 ? "A AND B STORED: SLIDE MORPH" : sn ? "B STORED" : "A STORED");
            } else if (pe->snaps == 3) {
                edit_grab_slider(ED_PARAMS, ED_SLIDER_X, ED_SLIDER_W, x);
            } else {
                notice("STORE A AND B FIRST");
            }
            break;
    }
}

// --- the KIT view ---------------------------------------------------------

static int s_kit_step_dir, s_kit_step_frames;

static void kit_hear(int lane) {
    int n[MUSIC_MAX_NOTES];
    music_key_notes(lane, PLAY_DRUM, 0, n);
    engine_note_on(SYNTH_DRUMS, n[0], 0.85f);
    if (lane >= 0 && lane < DRUM_LANES) s_app.drum_flash[lane] = 8;
}

static void kit_slider(int x) {
    const int l = s_app.kit_lane;
    if (s_app.kit_grab == 1) {
        const float v = ui_kit_slider(x, KIT_TUNE_X);
        const int semis = (int)lrintf(v * 2 * DRUM_TUNE_MAX) - DRUM_TUNE_MAX;
        if (semis == s_app.kit.lane[l].tune) return;
        drum_lane_set_tune(&s_app.kit, l, semis);
    } else {
        const int pct = (int)lrintf(ui_kit_slider(x, KIT_LEVEL_X) * DRUM_LEVEL_MAX / 5.0f) * 5;  // steps of 5%
        if (pct == s_app.kit.lane[l].level) return;
        drum_lane_set_level(&s_app.kit, l, pct);
    }
    apply_kit();
}

static void kit_step(int dir) {
    drum_lane_step(&s_app.kit, s_app.kit_lane, dir);
    apply_kit();
    kit_hear(s_app.kit_lane);
}

static void kit_touch(int x, int y) {
    s_touch = TOUCH_IGNORE;
    const int hit = ui_hit_kit(x, y);
    if (hit == KIT_HIT_PREV || hit == KIT_HIT_NEXT) {  // another stock kit
        const int n = drum_kit_count(), k = (s_app.kit.kit + (hit == KIT_HIT_NEXT ? 1 : n - 1)) % n;
        drum_kit_stock(&s_app.kit, k);
        apply_kit();
        kit_hear(0);
        char msg[sizeof(s_app.notice)];
        snprintf(msg, sizeof(msg), "KIT %d/%d: %s", k + 1, n, drum_kit_name(k));
        notice(msg);
    } else if (hit == KIT_HIT_NAME) {  // hear the kit, lane by lane... just the picked one
        kit_hear(s_app.kit_lane);
    } else if (hit >= KIT_HIT_LANE) {
        s_app.kit_lane = hit - KIT_HIT_LANE;
        kit_hear(s_app.kit_lane);
    } else if (hit == KIT_HIT_SPREV || hit == KIT_HIT_SNEXT) {
        s_kit_step_dir = hit == KIT_HIT_SNEXT ? 1 : -1;
        s_kit_step_frames = 0;
        kit_step(s_kit_step_dir);
        s_touch = TOUCH_KIT_STEP;
    } else if (hit == KIT_HIT_MIC) {  // the MIC take's START..END onto this lane
        int first, n = sampler_region(&s_app.sample_edit, s_take_frames, -1, &first);
        if (s_take_frames <= 0 || n <= 0) {
            notice("RECORD OR LOAD A MIC SAMPLE FIRST");
        } else if (lane_take(s_app.kit_lane, s_take_buf + first, n)) {
            kit_hear(s_app.kit_lane);
            char msg[sizeof(s_app.notice)];
            snprintf(msg, sizeof(msg), "MIC SAMPLE ON %s", drum_lane_name(s_app.kit_lane));
            notice(msg);
        }
    } else if (hit == KIT_HIT_TUNE || hit == KIT_HIT_LEVEL) {
        s_app.kit_grab = hit == KIT_HIT_TUNE ? 1 : 2;
        kit_slider(x);
        s_touch = TOUCH_KIT_SLIDER;
    }
}

// MIC editor, chopped: the slices onto the drum lanes, one each (KICK gets
// slice 1...), and the KIT opens to show them.
static void slices_to_kit(void) {
    const SampleEdit *e = &s_app.sample_edit;
    const int n = e->slices < DRUM_LANES ? e->slices : DRUM_LANES;
    int done = 0;
    for (int k = 0; k < n; k++) {
        int first, len = sampler_region(e, s_take_frames, k, &first);
        done += lane_take(k, s_take_buf + first, len);
    }
    char msg[sizeof(s_app.notice)];
    snprintf(msg, sizeof(msg), "%d SLICES ON THE DRUM LANES", done);
    notice(msg);
    s_app.kit_view = true;
    s_app.edit_view = false;
}

// A touch on the SYNTH page that's the EDIT / PRESETS tiles or the EDIT view.
static bool edit_touch(int x, int y) {
    const int extra = ui_hit_synth_extra(x, y);
    if (extra == SYN_HIT_EDIT) {
        s_app.edit_view = !s_app.edit_view;
        s_app.kit_view = false;
        s_touch = TOUCH_IGNORE;
        return true;
    }
    if (extra == SYN_HIT_KIT) {
        s_app.kit_view = !s_app.kit_view;
        s_app.edit_view = false;
        s_touch = TOUCH_IGNORE;
        return true;
    }
    if (s_app.kit_view && ui_in_synth_pad(x, y)) {
        kit_touch(x, y);
        return true;
    }
    if (extra == SYN_HIT_PRESETS) {
        open_presets();
        s_touch = TOUCH_IGNORE;
        return true;
    }
    if (!s_app.edit_view || !ui_in_synth_pad(x, y)) return false;
    s_touch = TOUCH_IGNORE;
    const int hit = ui_hit_edit(x, y);
    if (hit >= EDH_TAB && hit < EDH_TAB + EDP_PAGES) {
        s_app.edit_page = hit - EDH_TAB;
    } else if (hit == EDH_RESET) {
        release_all();
        edit_reset_sound();
        reload_sound();
        notice("EDIT RESET: THE SOUND AS DESIGNED");
    } else if (hit == EDH_BODY) {
        SynthEdit *e = &part_edit()->cur;
        switch (s_app.edit_page) {
            case EDP_ENV: {
                float hx[3], hy[3];
                ui_env_handles(e->p, hx, hy);
                int best = -1;
                float bd = 26.0f * 26.0f;
                for (int h = 0; h < 3; h++) {
                    const float d = (hx[h] - x) * (hx[h] - x) + (hy[h] - y) * (hy[h] - y);
                    if (d < bd) bd = d, best = h;
                }
                if (best >= 0) {
                    s_app.edit_grab = best;
                    memcpy(s_edit_tmp, e->p, sizeof(s_edit_tmp));
                    s_touch = TOUCH_EDIT_ENV;
                }
                break;
            }
            case EDP_FILTER:
            case EDP_LFO:
                if (y < ED_BODY_Y + ED_PAD_H + 2) {
                    s_touch = TOUCH_EDIT_PAD;
                    edit_pad(x, y);
                } else if (y >= ED_LOW_Y - 3) {
                    if (s_app.edit_page == EDP_FILTER) {
                        edit_grab_slider(ED_FENV, ED_BODY_X, ED_BODY_W, x);
                    } else {
                        int t = (x - ED_BODY_X) / 48;
                        e->lfo_target = (uint8_t)(t < 0 ? 0 : t >= LFO_TARGETS ? LFO_TARGETS - 1 : t);
                        if (!((e->set >> ED_LFO_DEPTH) & 1u)) edit_set(e, ED_LFO_DEPTH, 0.4f);  // so you hear it
                        apply_edit();
                    }
                }
                break;
            default: edit_voice_touch(x, y); break;
        }
    }
    return true;
}

// DRAW: the stylus draws the wave across the pad (left to right is one
// cycle); the sound updates as you go.
static void draw_touch(int x, int y) {
    float nx, ny;
    ui_synth_pad_pos(x, y, &nx, &ny);
    s_app.pad_touch = true;
    s_app.pad_x = nx;
    s_app.pad_y = ny;
    DrawWave *w = &s_app.parts[s_app.part].wave;
    const float wy = ny * 2.0f - 1.0f;  // up is +
    draw_stroke(w, s_draw_started ? s_draw_x : nx, s_draw_started ? s_draw_y : wy, nx, wy);
    s_draw_started = true;
    s_draw_x = nx;
    s_draw_y = wy;
    if (s_app.frame % 3 == 0) upload_wave(s_app.part);  // a few times a second is plenty
}

static void touch_begin(int x, int y) {
    int tab = ui_hit_tab(x, y);
    if (tab >= 0) {
        s_app.page = (Page)tab;
        s_app.confirm_name[0] = '\0';  // a REPLACE prompt never outlives the page
        s_touch = TOUCH_IGNORE;
        return;
    }
    if (ui_hit_key(x, y) >= 0) {
        s_touch = TOUCH_KEYS;
        return;
    }
    if (s_app.page == PAGE_MIX) {
        if (ui_hit_mixdown(x, y)) {
            mixdown_button();
            s_touch = TOUCH_IGNORE;
            return;
        }
        mix_begin(x, y);
        return;
    }
    if (s_app.page == PAGE_TAPE) {
        int hit = ui_hit_tape(x, y);
        s_touch = TOUCH_IGNORE;
        // A track's source chip changes the source once that track is
        // picked; until then it's just part of the sticker (pick / hold to wipe).
        if (hit >= TAPE_HIT_SOURCE && hit - TAPE_HIT_SOURCE != s_app.tape.track)
            hit = TAPE_HIT_TRACK + (hit - TAPE_HIT_SOURCE);
        if (hit >= TAPE_HIT_SOURCE) {  // ALL -> DRUMS -> PART 1 -> 2 -> 3 -> ALL
            int k = hit - TAPE_HIT_SOURCE, src = s_app.tape.source[k];
            tape_set_source(k, src == TAPE_SRC_ALL ? MIX_DRUMS : src == MIX_DRUMS ? 0 : src + 1 < PARTS ? src + 1 : TAPE_SRC_ALL);
        } else if (hit >= TAPE_HIT_MUTE) {
            int k = hit - TAPE_HIT_MUTE;
            tape_mute(k, !s_app.tape.muted[k]);
        } else if (hit >= TAPE_HIT_TRACK) {
            s_app.tape_hold_track = hit - TAPE_HIT_TRACK;
            s_tape_hold_frames = 0;
            s_touch = TOUCH_TAPE_TRACK;
        } else if (hit == TAPE_HIT_WINDOW) {
            s_scrub_last_x = x;
            s_scrub_v = 0.0f;
            tape_scrub(true, 0.0f);  // the tape stops under your finger
            s_touch = TOUCH_TAPE_SCRUB;
        } else if (hit == TAPE_HIT_SPEED) {
            s_touch = TOUCH_TAPE_SPEED;
        } else if (hit == TAPE_HIT_SAMPLE) {
            grab_from_tape();
        } else if (hit != TAPE_HIT_NONE) {
            tape_button(hit);
        }
        return;
    }
    if (s_app.page == PAGE_SYNTH && edit_touch(x, y)) return;
    if (s_app.page == PAGE_SYNTH && s_app.sound == sound_mic_index() && ui_hit_synth_rec(x, y)) {
        if (s_app.mic_ok) {
            release_all();
            mic_start();
            s_app.mic_rec = mic_recording();
        }
        s_touch = TOUCH_REC;
        return;
    }
    if (s_app.page == PAGE_SYNTH) {
        int part = ui_hit_part_tab(x, y);
        if (part >= 0) {  // tap: pick this part; hold: mute it
            s_app.part_hold = part;
            s_part_hold_frames = 0;
            s_touch = TOUCH_PART_HOLD;
            return;
        }
    }
    if (s_app.page == PAGE_SYNTH && ui_hit_synth_dice(x, y)) {
        roll_sound();
        s_touch = TOUCH_IGNORE;
        return;
    }
    if (s_app.page == PAGE_SYNTH && s_app.sound == sound_mic_index() && sampler_begin(x, y)) return;
    if (s_app.page == PAGE_SYNTH) {
        int tile = ui_hit_sound_tile(x, y);
        if (tile >= 0) {
            select_sound(tile);
            s_touch = TOUCH_IGNORE;
        } else if (ui_in_synth_pad(x, y)) {
            if (sound_get(s_app.sound)->kind == SOUND_BANK) {
                s_touch = TOUCH_BANK;
                s_paint_step = -1;
            } else if (s_app.sound == sound_draw_index() && ui_hit_draw_shape(x, y) >= 0) {
                draw_preset(&s_app.parts[s_app.part].wave, ui_hit_draw_shape(x, y));
                upload_wave(s_app.part);
                s_touch = TOUCH_IGNORE;
            } else if (s_app.sound == sound_draw_index()) {
                s_draw_started = false;
                s_touch = TOUCH_DRAW;
                draw_touch(x, y);
            } else {
                s_touch = TOUCH_SYNTH_PAD;
            }
        } else {
            s_touch = TOUCH_IGNORE;
        }
        return;
    }
    if (s_app.page == PAGE_FX) {
        int hit = ui_hit_fx_sticker(x, y), step = ui_hit_fx_auto(x, y);
        if (step >= 0) step = page_step(step);
        if (hit >= 0) {
            fx_sticker(hit);
            s_touch = TOUCH_IGNORE;
        } else if (step >= 0 && s_app.pattern.fx[step]) {  // tap a recorded step: erase it
            seq_set_fx(step, 0, 0.0f, 0.0f);
            s_touch = TOUCH_IGNORE;
        } else {
            s_touch = ui_in_fx_pad(x, y) ? TOUCH_FX_PAD : TOUCH_IGNORE;
        }
        return;
    }
    if (s_app.page == PAGE_SONGS) {
        songs_touch(x, y);
        return;
    }
    if (s_app.page == PAGE_SOUND && ui_hit_songs_button(x, y)) {
        open_library(false);
        s_touch = TOUCH_IGNORE;
        return;
    }
    if (s_app.page == PAGE_SOUND) {
        const int row = ui_hit_sound_rows(x, y);
        if (row != SND_HIT_NONE) {
            sound_row(row);
            s_touch = TOUCH_IGNORE;
            return;
        }
        if (ui_hit_theme(x, y)) {
            ui_set_theme(next_theme());
            s_touch = TOUCH_IGNORE;
            return;
        }
        s_app.active_slider = ui_hit_slider(x, y);
        s_touch = s_app.active_slider >= 0 ? TOUCH_SLIDER : TOUCH_IGNORE;
        return;
    }
    int slot = ui_hit_seq_slot(x, y);
    if (slot >= 0) {
        if (slot == SEQ_HIT_BAR) {  // tap: next bar on screen; hold: the pattern's length
            s_app.bar_hold = 0;
            s_touch = TOUCH_BAR_BTN;
        } else if (slot == SEQ_HIT_ROLL) {
            s_app.roll_view = s_app.arr_view ? true : !s_app.roll_view;
            s_app.arr_view = false;
            s_touch = TOUCH_IGNORE;
        } else if (slot == SEQ_HIT_SONG) {  // tap: SONG on/off; hold: the SONG sheet
            s_app.song_btn_hold = 0;
            s_touch = TOUCH_SONG_BTN;
        } else if (slot == SEQ_HIT_SWING) {
            seq_set_swing(ui_swing_at(x));
            s_touch = TOUCH_SWING;
        } else {
            s_app.slot_hold = slot;  // tap = switch, hold = copy here
            s_slot_frames = 0;
            s_touch = TOUCH_SLOT;
        }
        return;
    }
    SeqButton b = ui_hit_seq_button(x, y);
    if (b != SEQ_BTN_NONE) {
        press_button(b);
        s_button = b;
        s_button_frames = 0;
        s_touch = TOUCH_BUTTON;
        return;
    }
    if (s_app.arr_view) {
        arr_touch(x, y);
        return;
    }
    if (s_app.roll_view) {
        roll_begin(x, y);
        return;
    }
    if (ui_hit_seq_part_chip(x, y)) {  // the synth row's chip: next part
        switch_part((s_app.part + 1) % PARTS);
        s_touch = TOUCH_IGNORE;
        return;
    }
    int step, row;
    if (ui_hit_seq_cell(x, y, &step, &row)) {
        // The first cell decides: paint "on" over empty cells, "off" over
        // full ones, for the rest of the drag along this row.
        s_paint_row = row;
        s_paint_on = !cell_on(step, row);
        paint_cell(step, row);
        s_touch = TOUCH_CELLS;
        return;
    }
    s_touch = TOUCH_IGNORE;
}

static void handle_touch(u32 down, u32 held, u32 up) {
    if (up & KEY_TOUCH) {
        if (s_touch == TOUCH_FX_PAD) fx_pad_release();
        if (s_touch == TOUCH_ROLL_NOTE) roll_end();
        if (s_touch == TOUCH_PART_HOLD && s_part_hold_frames < PART_MUTE_FRAMES) switch_part(s_app.part_hold);
        s_app.part_hold = -1;
        s_app.part_hold_t = 0.0f;
        if (s_touch == TOUCH_REC && s_app.mic_rec) finish_recording();
        if (s_touch == TOUCH_TAPE_SCRUB) tape_scrub(false, 0.0f);
        if (s_touch == TOUCH_TAPE_TRACK && s_tape_hold_frames < TAPE_WIPE_FRAMES) tape_select(s_app.tape_hold_track);
        s_app.tape_hold_track = -1;
        s_app.tape_hold = 0.0f;
        if (s_touch == TOUCH_SLOT && s_slot_frames < SLOT_COPY_FRAMES) {
            seq_select_slot(s_app.slot_hold);
            if (s_app.arr_view) arr_assign(s_app.slot_hold);  // SONG sheet: the picked row plays this slot
        }
        if (s_touch == TOUCH_BUTTON && s_button == SEQ_BTN_CLEAR && s_button_frames < UNDO_HOLD_FRAMES) clear_pattern();
        s_app.clear_hold_t = 0.0f;
        if (s_touch == TOUCH_SONG_BTN && s_app.song_btn_hold >= 0) {
            const bool on = !s_app.seq.song;
            seq_set_song(on);
            SeqArrangement a;
            seq_get_arrangement(&a);
            if (on && a.count == 0 && !s_app.arr_view) notice("SONG ON: HOLD SONG TO ARRANGE");
        }
        s_app.song_btn_hold = -1;
        if (s_touch == TOUCH_BAR_BTN && s_app.bar_hold >= 0)
            s_app.bar_page = (s_app.bar_page + 1) % seq_bars(&s_app.pattern);
        s_app.bar_hold = -1;
        s_app.slot_hold = -1;
        s_app.roll_active = -1;
        s_app.mix_strip = -1;
        s_app.mix_grab = MIX_GRAB_NONE;
        if (s_touch == TOUCH_SMP && (s_app.smp_grab <= 2 || s_app.smp_grab >= SMP_GRAB_CUT)) {  // START / END / a cut
            SampleEdit *e = &s_app.sample_edit;  // lands on a zero crossing, so the slice doesn't click
            if (s_app.smp_grab == 1) e->start = sampler_snap(s_take_buf, s_take_frames, e->start);
            if (s_app.smp_grab == 2) e->end = sampler_snap(s_take_buf, s_take_frames, e->end);
            if (s_app.smp_grab >= SMP_GRAB_CUT) {
                const int k = s_app.smp_grab - SMP_GRAB_CUT;
                e->cut[k] = sampler_snap(s_take_buf, s_take_frames, e->cut[k]);
            }
            reload_take();
        }
        s_app.smp_grab = 0;
        s_app.slot_hold_t = 0.0f;
        s_app.song_hold = -1;
        s_app.song_hold_t = 0.0f;
        s_app.pad_touch = false;
        s_app.edit_grab = -1;
        s_app.kit_grab = 0;
        source_release(SOURCE_TOUCH);
        s_app.active_slider = -1;
        s_touch = TOUCH_NONE;
        return;
    }
    if (!(held & KEY_TOUCH)) return;

    const touchPosition tp = s_tp;
    if (down & KEY_TOUCH) touch_begin(tp.px, tp.py);

    switch (s_touch) {
        case TOUCH_SLIDER:
            // A drag stays on the slider it started on.
            set_param(s_app.active_slider, ui_slider_value(tp.py));
            break;
        case TOUCH_KEYS: {
            // Sliding across keys re-triggers, like a ribbon.
            int key = ui_hit_key(tp.px, tp.py);
            if (key < 0)
                source_release(SOURCE_TOUCH);
            else if (key != s_held[SOURCE_TOUCH].key)
                source_press(SOURCE_TOUCH, key);
            break;
        }
        case TOUCH_CELLS: {
            int step, row;
            if (ui_hit_seq_cell(tp.px, tp.py, &step, &row) && row == s_paint_row && step != s_paint_step)
                paint_cell(step, row);
            break;
        }
        case TOUCH_SYNTH_PAD: {
            float nx, ny;
            ui_synth_pad_pos(tp.px, tp.py, &nx, &ny);
            s_app.pad_touch = true;
            s_app.pad_x = nx;
            s_app.pad_y = ny;
            set_macro(0, nx);
            set_macro(1, ny);
            break;
        }
        case TOUCH_EDIT_ENV:
            ui_env_drag(s_app.edit_grab, tp.px, tp.py, s_edit_tmp);
            edit_env_from(s_edit_tmp);
            break;
        case TOUCH_EDIT_PAD: edit_pad(tp.px, tp.py); break;
        case TOUCH_EDIT_SLIDER: edit_slider_move(tp.px); break;
        case TOUCH_DRAW: draw_touch(tp.px, tp.py); break;
        case TOUCH_KIT_SLIDER: kit_slider(tp.px); break;
        case TOUCH_KIT_STEP:  // held: after a moment, step on every few frames
            if (++s_kit_step_frames > 20 && s_kit_step_frames % 4 == 0) kit_step(s_kit_step_dir);
            break;
        case TOUCH_BANK: {
            // Slide across the dots to audition patches.
            int cell = ui_hit_bank_cell(tp.px, tp.py);
            const Sound *snd = sound_get(s_app.sound);
            if (cell >= 0 && cell < snd->bank_count && cell != s_paint_step) {
                s_paint_step = cell;
                change_patch(snd->bank_first + cell);
            }
            break;
        }
        case TOUCH_FX_PAD: {
            float nx, ny;
            ui_fx_pad_pos(tp.px, tp.py, &nx, &ny);
            s_app.pad_touch = true;
            s_app.pad_x = nx;
            s_app.pad_y = ny;
            fx_pad_move(nx, ny);
            break;
        }
        case TOUCH_TAPE_SCRUB: {
            // Drag the tape like a record: 1 px of finger = 1 px of tape.
            const float secs_per_px = TAPE_WIN_SECONDS / TAPE_WIN_W;
            float v = -(tp.px - s_scrub_last_x) * secs_per_px * 60.0f;
            s_scrub_v = s_scrub_v * 0.5f + v * 0.5f;
            tape_scrub(true, s_scrub_v);
            s_scrub_last_x = tp.px;
            break;
        }
        case TOUCH_SWING: seq_set_swing(ui_swing_at(tp.px)); break;
        case TOUCH_ROLL_NOTE: roll_drag(tp.px, tp.py); break;
        case TOUCH_MIX: mix_drag(tp.py); break;
        case TOUCH_SMP: sampler_drag(tp.px); break;
        case TOUCH_BAR_BTN:
            if (s_app.bar_hold >= 0 && ++s_app.bar_hold >= BAR_LEN_FRAMES) {
                const int bars = seq_bars(&s_app.pattern), next = bars == 1 ? 2 : bars == 2 ? 4 : 1;
                seq_set_bars(next);
                char msg[sizeof(s_app.notice)];
                snprintf(msg, sizeof(msg), "PATTERN %c: %d BAR%s", 'A' + s_app.seq.slot, next, next > 1 ? "S" : "");
                notice(msg);
                s_app.bar_hold = -1;  // done: the release does nothing
            }
            break;
        case TOUCH_SONG_BTN:
            if (s_app.song_btn_hold >= 0 && ++s_app.song_btn_hold >= SONG_SHEET_FRAMES) {
                s_app.arr_view = !s_app.arr_view;
                if (s_app.arr_view) seq_set_song(true);  // you're arranging a song
                s_app.song_btn_hold = -1;  // done: the release does nothing
            }
            break;
        case TOUCH_PART_HOLD:
            if (s_part_hold_frames < PART_MUTE_FRAMES && ++s_part_hold_frames == PART_MUTE_FRAMES) {
                int k = s_app.part_hold;
                seq_set_part_mute(k, !((s_app.seq.muted >> k) & 1u));
            }
            s_app.part_hold_t = (float)s_part_hold_frames / PART_MUTE_FRAMES;
            break;
        case TOUCH_ROLL_KEYS: {
            // Drag the keyboard: it moves with your finger, a row at a time.
            int dy = tp.py - s_keys_last_y;
            if (dy >= ROLL_ROW_H || dy <= -ROLL_ROW_H) {
                roll_scroll_by(dy / ROLL_ROW_H);
                s_keys_last_y += (dy / ROLL_ROW_H) * ROLL_ROW_H;
            }
            break;
        }
        case TOUCH_SONG_HOLD:
            if (s_song_hold_frames < SONG_HOLD_FRAMES && ++s_song_hold_frames == SONG_HOLD_FRAMES)
                song_hold_done(s_app.song_hold);
            s_app.song_hold_t = (float)s_song_hold_frames / SONG_HOLD_FRAMES;
            break;
        case TOUCH_SLOT:
            if (s_slot_frames < SLOT_COPY_FRAMES && ++s_slot_frames == SLOT_COPY_FRAMES) {
                undo_save(s_app.slot_hold);
                seq_copy_to_slot(s_app.slot_hold);  // a variation, ready to edit
                ui_key_struck(s_app.slot_hold, PLAY_CHORD);
            }
            s_app.slot_hold_t = (float)s_slot_frames / SLOT_COPY_FRAMES;
            break;
        case TOUCH_TAPE_SPEED:
            s_tape_speed = ui_tape_speed_at(tp.px);
            tape_set_speed(s_tape_speed);
            break;
        case TOUCH_TAPE_TRACK:
            if (s_tape_hold_frames < TAPE_WIPE_FRAMES && ++s_tape_hold_frames == TAPE_WIPE_FRAMES) {
                tape_clear(s_app.tape_hold_track);
                ui_key_struck(s_app.tape_hold_track * 2, PLAY_DRUM);  // poof
            }
            s_app.tape_hold = (float)s_tape_hold_frames / TAPE_WIPE_FRAMES;
            break;
        case TOUCH_BUTTON:
            s_button_frames++;
            if (s_button == SEQ_BTN_CLEAR) {  // hold ✕: undo
                s_app.clear_hold_t = s_button_frames / (float)UNDO_HOLD_FRAMES;
                if (s_button_frames == UNDO_HOLD_FRAMES) undo();
            }
            if ((s_button == SEQ_BTN_BPM_DOWN || s_button == SEQ_BTN_BPM_UP) && s_button_frames > REPEAT_DELAY &&
                s_button_frames % REPEAT_EVERY == 0)
                press_button(s_button);
            break;
        default: break;
    }
}

#ifdef VM_STORY
static int s_story_cpad_x;  // the script's circle pad (x), for bends
#endif
static void handle_buttons(u32 down, u32 held, u32 up, u32 repeat) {
    s_app.mode = (held & KEY_L) ? PLAY_CHORD : (held & KEY_R) ? PLAY_DRUM : PLAY_NOTE;

    for (int k = 0; k < MUSIC_KEYS; k++) {
        if (down & kKeyButtons[k]) source_press(k, k);
        if (up & kKeyButtons[k]) source_release(k);
    }

    // SELECT held: the circle pad bends the pitch (springs back when you
    // let go). SELECT tapped without bending: next sound.
    static bool s_bent;
    if (held & KEY_SELECT) {
        circlePosition c;
        hidCircleRead(&c);
#ifdef VM_STORY
        c.dx = (s16)s_story_cpad_x;
#endif
        float b = c.dx / 150.0f;
        b = b < -1.0f ? -1.0f : b > 1.0f ? 1.0f : b;
        if (fabsf(b) < 0.08f) b = 0.0f;  // a dead zone, so a resting pad doesn't detune
        if (b != 0.0f) s_bent = true;
        if (fabsf(b - s_app.bend) > 0.01f || (b == 0.0f && s_app.bend != 0.0f)) {
            s_app.bend = b;
            engine_pitch_bend(b * 2.0f);  // +-2 semitones
        }
    } else if (s_app.bend != 0.0f) {
        s_app.bend = 0.0f;
        engine_pitch_bend(0.0f);
    }
    if (up & KEY_SELECT) {
        if (!s_bent) select_sound(s_app.sound + 1);
        s_bent = false;
    }
    // Circle pad left/right: next patch inside a bank, otherwise next sound;
    // up/down: octave. (Not while SELECT is held: then it's bending.)
    if (!(held & KEY_SELECT)) {
        const bool bank = sound_get(s_app.sound)->kind == SOUND_BANK;
        if (repeat & KEY_CPAD_RIGHT) bank ? change_patch(s_app.patch + 1) : select_sound(s_app.sound + 1);
        if (repeat & KEY_CPAD_LEFT) bank ? change_patch(s_app.patch - 1) : select_sound(s_app.sound - 1);
        if ((down & KEY_CPAD_UP) && s_app.octave < MUSIC_OCTAVE_MAX) s_app.octave++;
        if ((down & KEY_CPAD_DOWN) && s_app.octave > MUSIC_OCTAVE_MIN) s_app.octave--;
    }

    // START: tap to play/stop; holding it quits (checked in the main loop).
    if (down & KEY_START) s_start_down = s_app.frame;
    if ((up & KEY_START) && s_app.frame - s_start_down < PLAY_TAP_FRAMES) start_pressed();
}

static void update_lit_keys(void) {
    memset(s_app.key_lit, 0, sizeof(s_app.key_lit));
    memset(s_app.live_pitch, 0, sizeof(s_app.live_pitch));
    for (int s = 0; s < NUM_SOURCES; s++) {
        const Held *h = &s_held[s];
        if (h->key < 0) continue;
        s_app.key_lit[h->key] = true;
        if (h->synth == SYNTH_DRUMS) continue;
        for (int i = 0; i < h->count; i++)  // what's sounding live, for the roll
            if (h->notes[i] >= 0 && h->notes[i] < 128) s_app.live_pitch[h->notes[i] >> 3] |= (uint8_t)(1u << (h->notes[i] & 7));
    }
    if (s_audition_pitch >= 0 && s_audition_pitch < 128)
        s_app.live_pitch[s_audition_pitch >> 3] |= (uint8_t)(1u << (s_audition_pitch & 7));
    for (int k = 0; k < MUSIC_KEYS; k++)
        if (s_flash[k] > 0) {
            s_app.key_lit[k] = true;
            s_flash[k]--;
        }
}

// ---------------------------------------------------------------- main ----

#ifdef VM_STORY
// ---------------------------------------------------------------- STORY ----
// make STORY=1: a scripted session that makes a song from nothing to a
// mixdown, by playing the 3DS the way a person would: stylus taps, drags
// and holds that go through the real touch handlers (so it's an end-to-end
// test of the UI as well as the demo video). Captions say what's happening.

typedef enum { S_CAP, S_WAIT, S_WAITSEC, S_TAP, S_HOLD, S_DRAG, S_ROLL, S_CALL, S_KEYS, S_DRAGH } StoryKind;
typedef struct {
    StoryKind k;
    int a, b, c, d, n;  // TAP/HOLD: x y; DRAG: x y x2 y2 frames; ROLL: step pitch len
    const char *text;   // CAP
    void (*fn)(void);   // CALL
} StoryEv;

#define CAP(num, t) {S_CAP, num, 0, 0, 0, 0, t, NULL}
#define WAIT(f) {S_WAIT, 0, 0, 0, 0, f, NULL, NULL}
#define WAITSEC(s) {S_WAITSEC, 0, 0, 0, 0, s, NULL, NULL}  // seconds of (emulated) time, whatever the frame rate
#define TAP(x, y) {S_TAP, x, y, 0, 0, 5, NULL, NULL}
#define HOLD(x, y, f) {S_HOLD, x, y, 0, 0, f, NULL, NULL}
#define HOLDMS(x, y, ms) {S_HOLD, x, y, ms, 0, 0, NULL, NULL}  // held for real (emulated) time
#define DRAG(x, y, x2, y2, f) {S_DRAG, x, y, x2, y2, f, NULL, NULL}
#define ROLL(step, pitch, len) {S_ROLL, step, pitch, len, 0, 0, NULL, NULL}
#define CALL(f) {S_CALL, 0, 0, 0, 0, 0, NULL, f}
#define KEYS(mask) {S_KEYS, (int)(mask), 0, 0, 0, 0, NULL, NULL}  // buttons held from now on
#define PRESS(mask, f) KEYS(mask), WAIT(f), KEYS(0), WAIT(5)          // a button tapped, held f frames
#define DRAGH(h, x2, y2, f) {S_DRAGH, h, 0, x2, y2, f, NULL, NULL}   // an EDIT envelope handle, dragged
#define ROLLTAP(step, pitch) {S_ROLL, step, pitch, 0, 0, 0, NULL, NULL}  // a tap on a roll note

// Where things are (the middles of the stickers the hit tests answer for).
#define TABX(p) (TAB_X0 + (p) * TAB_PITCH + TAB_W / 2)
#define TABY (TAB_Y + TAB_H / 2)
#define GX(s) (SEQ_GRID_X + (s) * SEQ_CELL_W + SEQ_CELL_W / 2)
#define DY(l) (SEQ_DRUM_Y + (l) * SEQ_DRUM_PITCH + SEQ_DRUM_H / 2)
#define SLX(i) (SEQ_SLOT_X0 + (i) * SEQ_SLOT_PITCH + SEQ_SLOT_W / 2)
#define SLY (SEQ_SLOT_Y + SEQ_SLOT_H / 2)
#define SONGX (SEQ_SONG_X + SEQ_SONG_W / 2)
#define ROLLX (SEQ_ROLL_X + SEQ_ROLL_W / 2)
#define PLAYX (SEQ_PLAY_X + SEQ_BTN_W / 2)
#define RECX (SEQ_REC_X + SEQ_BTN_W / 2)
#define TILEX(i) (SYN_TILE_X0 + ((i) % SYN_TILE_COLS) * SYN_TILE_PITCH_X + SYN_TILE_W / 2)
#define TILEY(i) (SYN_TILE_Y0 + ((i) / SYN_TILE_COLS) * SYN_TILE_PITCH_Y + SYN_TILE_H / 2)
#define PICKX(k) (ROLL_PART_X0 + (k) * ROLL_PART_PITCH + ROLL_PART_W / 2)
#define PICKY (ROLL_TOGGLE_Y + ROLL_TOGGLE_H / 2)
#define KEYX(k) ((k) * KEY_W + KEY_W / 2)
#define KEYY (KEYS_Y0 + 30)
#define STRIPX(k) (MIX_X0 + (k) * MIX_PITCH)
#define KNOBX(k, i) (STRIPX(k) + MIX_KNOB_X(i))
#define KNOBY(i) MIX_KNOB_Y(i)
#define CAPY(level) ((int)(MIX_FADER_Y + MIX_FADER_H - 5 - (level) * (MIX_FADER_H - 10)))
#define FADERX(k) (STRIPX(k) + MIX_FADER_DX + MIX_FADER_W / 2)
#define FXSX (FX_STICK_X + FX_STICK_W / 2)
#define FXSY(i) (FX_STICK_Y0 + (i) * FX_STICK_PITCH + FX_STICK_H / 2)
#define CELLX(i) (ARR_X0 + ((i) % ARR_COLS) * ARR_PITCH_X + ARR_CELL_W / 2)
#define CELLY(i) (ARR_Y0 + ((i) / ARR_COLS) * ARR_PITCH_Y + ARR_CELL_H / 2)
#define CHIPX(k) (ARR_MUTE_X0 + (k) * ARR_MUTE_PITCH + ARR_MUTE_W / 2)
#define CHIPY (ARR_HEAD_Y + ARR_HEAD_H / 2)
#define ROOTX(up) (ROW_X0 + ((up) ? ROOT_W * 3 / 4 : ROOT_W / 4))
#define SCALEX(next) (SCALE_X + ((next) ? SCALE_W * 3 / 4 : SCALE_W / 4))
#define KEYROWY (KEYROW_Y + ROW_H / 2)
#define ARPX(i) (ROW_X0 + (i) * ARP_PITCH + ARP_W / 2)
#define ARPROWY (ARPROW_Y + ROW_H / 2)

enum { SND_JUNO = 0, SND_SQUELCH = 3, SND_BELL = 4 };

static void story_blank(void) {  // a new song, really empty
    song_delete(SONGS_ROOT, "MY FIRST SONG");  // from a previous run: SAVE AS would ask to replace it
    new_song();
    seq_clear();
    seq_set_bpm(108);
    switch_part(0);
    s_app.page = PAGE_SOUND;
    s_app.roll_view = false;
    s_app.arr_view = false;
}
#ifdef VM_RECORD
static void rec_start(void) {
    mkdir(REC_DIR, 0777);
    s_rec_top = fopen(REC_DIR "/top.raw", "wb");
    s_rec_bot = fopen(REC_DIR "/bottom.raw", "wb");
    s_rec_times = fopen(REC_DIR "/times.bin", "wb");
    if (s_rec_top) setvbuf(s_rec_top, NULL, _IOFBF, 256 * 1024);
    if (s_rec_bot) setvbuf(s_rec_bot, NULL, _IOFBF, 256 * 1024);
    wav_stream_open(&s_rec_audio, REC_DIR "/audio.wav", engine_sample_rate(), 2);
    audio_out_capture(true);
    s_rec_on = s_rec_top && s_rec_bot && s_rec_times;
}
static void rec_stop(void) {
    if (!s_rec_on) return;
    mixdown_drain();
    s_rec_on = false;
    audio_out_capture(false);
    fclose(s_rec_top);
    fclose(s_rec_bot);
    fclose(s_rec_times);
    wav_stream_close(&s_rec_audio);
    FILE *f = fopen(REC_DIR "/done", "wb");  // the host waits for this
    if (f) fclose(f);
}
// Called with the GPU idle (right after a synced frame begin): the back
// buffers hold a finished frame.
static void rec_frame(void) {
    if (!s_rec_on || s_app.frame % REC_EVERY) return;
    // When it was, by the sound's clock (writing frames slows the frame
    // loop, so frame counts aren't time; the host lines frames up by this).
    const uint32_t t = s_rec_audio.frames;
    fwrite(&t, sizeof(t), 1, s_rec_times);
    u16 w, h;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
    fwrite(fb, 3, (size_t)w * h, s_rec_top);
    fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    fwrite(fb, 3, (size_t)w * h, s_rec_bot);
}
#else
static void rec_start(void) {}
static void rec_stop(void) {}
#endif

#ifndef VM_TOUR
static void story_scroll_bass(void) { s_app.roll_scroll = music_roll_row(36, false); }    // C2 at the bottom
static void story_scroll_chords(void) { s_app.roll_scroll = music_roll_row(57, false); }  // A3 at the bottom
#endif

#ifdef VM_TOUR
// --- the feature tour (make STORY=1 TOUR=1): every feature, one at a time ---

#define EDITX (SYN_TILE_X0 + SYN_EDIT_W / 2)
#define EDITY (SYN_EDIT_Y + SYN_TILE_H / 2)
#define KITHEADY (KIT_HEAD_Y + KIT_HEAD_H / 2)
#define KITLANEX(l) (KIT_LANE_X0 + ((l) % 2) * KIT_LANE_PITCH_X + KIT_LANE_W / 2)
#define KITLANEY(l) (KIT_LANE_Y0 + ((l) / 2) * KIT_LANE_PITCH_Y + KIT_LANE_H / 2)
#define KITSMPY (KIT_SMP_Y + KIT_SMP_H / 2)
#define KITSLY (KIT_SLIDER_Y + KIT_SLIDER_H / 2)
#define ETABX(i) (ED_TAB_X0 + (i) * ED_TAB_PITCH + ED_TAB_W / 2)
#define ETABY (ED_TAB_Y + ED_TAB_H / 2)
#define EPADX(f) ((int)(ED_BODY_X + 6 + (f) * (ED_BODY_W - 12)))
#define EPADY(f) ((int)(ED_BODY_Y + 6 + (1.0f - (f)) * (ED_PAD_H - 12)))
#define ELOWY (ED_LOW_Y + ED_ROW_H / 2)
#define EROWY(r) (ED_ROW_Y0 + (r) * ED_ROW_PITCH + ED_ROW_H / 2)
#define ESLX(f) ((int)(ED_SLIDER_X + 3 + (f) * (ED_SLIDER_W - 6)))
#define EFULLX(f) ((int)(ED_BODY_X + 3 + (f) * (ED_BODY_W - 6)))
#define ECHIPX (ED_BODY_X + 20)
#define EUNIX(u) (ED_BODY_X + ((u) - 1) * (ED_CHIP_W + 3) + ED_CHIP_W / 2)
#define EABX(b) (ED_BODY_X + (b) * 37 + 17)
#define ETGTX(i) (ED_BODY_X + (i) * 48 + 23)
#define PADX(f) ((int)(SYN_PAD_X + 8 + (f) * (SYN_PAD_W - 16)))
#define PADY(f) ((int)(SYN_PAD_Y + 20 + (1.0f - (f)) * (SYN_PAD_H - 28)))
#define DOTX(c) (BANK_X0 + (c) * BANK_CELL_W + BANK_CELL_W / 2)
#define DOTY(r) (BANK_Y0 + (r) * BANK_CELL_H + BANK_CELL_H / 2)
#define SHAPEX(i) (DRAW_CHIP_X + (i) * DRAW_CHIP_PITCH + DRAW_CHIP_W / 2)
#define SHAPEY (DRAW_CHIP_Y + DRAW_CHIP_H / 2)
#define ACCY (SEQ_ACC_Y + SEQ_ACC_H / 2)
#define BARX (SEQ_BAR_X + SEQ_BAR_W / 2)
#define SWINGX(f) ((int)(SEQ_SWING_X + 3 + (f) * (SEQ_SWING_W - 6)))
#define CHOPX (SMP_CHOP_X + SMP_CHOP_W / 2)
#define CHOPY (SYN_DICE_Y + SYN_DICE_W / 2)
#define GRIPY (SMP_WAVE_Y + SMP_WAVE_H + SMP_GRIP_H / 2)
#define GRIPX(f) ((int)(SMP_WAVE_X + (f) * SMP_WAVE_W))
#define FXPX(f) ((int)(FX_PAD_X + 10 + (f) * (FX_PAD_W - 20)))
#define FXPY(f) ((int)(FX_PAD_Y + 10 + (1.0f - (f)) * (FX_PAD_H - 30)))
#define TBTNY (TAPE_BTN_Y + TAPE_BTN_H / 2)
#define TWINY (TAPE_WIN_Y + TAPE_WIN_H / 2)
#define SONGBTNY(i) (SONG_BTN_Y0 + (i) * SONG_BTN_PITCH + SONG_BTN_H / 2)
#define SONGBTNX (SONG_BTN_X + SONG_BTN_W / 2)
enum { T_JUNO, T_DX7, T_PLUCK, T_SQUELCH, T_BELL, T_BREEZE, T_SAW, T_FOLD, T_WAVY, T_MIC, T_DRAW, T_PRESET };

static void tour_setup(void) {
    story_blank();
    seq_set_bpm(112);
    s_story_name = "TOUR SONG";
    song_delete(SONGS_ROOT, "TOUR SONG");
    char path[160];  // a preset from an earlier run would make SAVE AS ask to replace it
    sample_path(PRESETS_ROOT, "WIDE WAVY" PRESET_EXT, path, sizeof(path));
    remove(path);
}
static void tour_name_preset(void) { s_story_name = "WIDE WAVY"; }
static void tour_name_song(void) { s_story_name = "TOUR SONG"; }
static void tour_bass_rows(void) { s_app.roll_scroll = music_roll_row(41, false); }   // F2 at the bottom
static void tour_chord_rows(void) { s_app.roll_scroll = music_roll_row(53, false); }  // F3 at the bottom
static void bend_0(void) { s_story_cpad_x = 0; }
static void bend_up(void) { s_story_cpad_x = 150; }
static void bend_half(void) { s_story_cpad_x = 75; }
static void bend_down(void) { s_story_cpad_x = -150; }

static const StoryEv kStory[] = {
    CAP(0, "VIBE MACHINE: EVERY FEATURE, ONE BY ONE"),
    CALL(tour_setup),
    CALL(rec_start),
    WAIT(120),

    CAP(1, "PLAY: D-PAD AND Y B A X ARE THE KEYS"),
    PRESS(KEY_DLEFT, 12), PRESS(KEY_DDOWN, 12), PRESS(KEY_DRIGHT, 12), PRESS(KEY_DUP, 12),
    PRESS(KEY_Y, 12), PRESS(KEY_B, 12), PRESS(KEY_A, 12), PRESS(KEY_X, 30),
    CAP(2, "HOLD L: EVERY KEY PLAYS A CHORD"),
    PRESS(KEY_L | KEY_DLEFT, 40), PRESS(KEY_L | KEY_DUP, 40), PRESS(KEY_L | KEY_Y, 40), PRESS(KEY_L | KEY_B, 50),
    CAP(3, "HOLD R: THE KEYS ARE DRUMS"),
    PRESS(KEY_R | KEY_DLEFT, 8), PRESS(KEY_R | KEY_DRIGHT, 8), PRESS(KEY_R | KEY_DDOWN, 8), PRESS(KEY_R | KEY_DLEFT, 8),
    PRESS(KEY_R | KEY_DLEFT, 8), PRESS(KEY_R | KEY_DRIGHT, 8), PRESS(KEY_R | KEY_DDOWN, 8), PRESS(KEY_R | KEY_A, 12),

    CAP(4, "SOUND PAGE: THE SONG'S KEY (A MINOR)"),
    TAP(ROOTX(0), KEYROWY), TAP(ROOTX(0), KEYROWY), TAP(ROOTX(0), KEYROWY),
    TAP(SCALEX(1), KEYROWY),
    PRESS(KEY_DLEFT, 10), PRESS(KEY_DRIGHT, 10), PRESS(KEY_Y, 10), PRESS(KEY_X, 20),

    CAP(5, "THE ARPEGGIATOR: HOLD A CHORD"),
    TAP(ARPX(0), ARPROWY),
    KEYS(KEY_L | KEY_DLEFT), WAITSEC(2), KEYS(0),
    TAP(ARPX(1), ARPROWY), TAP(ARPX(3), ARPROWY),
    KEYS(KEY_L | KEY_Y), WAITSEC(2), KEYS(0),
    TAP(ARPX(1), ARPROWY), TAP(ARPX(1), ARPROWY), TAP(ARPX(1), ARPROWY), TAP(ARPX(3), ARPROWY),
    TAP(ARPX(0), ARPROWY),

    CAP(6, "NINE COLOUR THEMES"),
    TAP(THEME_X + THEME_W / 2, THEME_Y + THEME_H / 2), WAIT(30),
    TAP(THEME_X + THEME_W / 2, THEME_Y + THEME_H / 2), WAIT(30),

    CAP(7, "STEP SEQUENCER: TAP IN A BEAT"),
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(GX(0), DY(0)), TAP(GX(6), DY(0)), TAP(GX(8), DY(0)), TAP(GX(11), DY(0)),
    TAP(GX(4), DY(1)), TAP(GX(12), DY(1)),
    DRAG(GX(0), DY(2), GX(15), DY(2), 60),
    TAP(PLAYX, TABY),
    WAIT(90),
    CAP(8, "ACCENTS HIT HARDER; SWING SHUFFLES"),
    TAP(GX(0), ACCY), TAP(GX(8), ACCY), TAP(GX(14), ACCY),
    DRAG(SWINGX(0.0f), SLY, SWINGX(0.45f), SLY, 30),
    WAIT(120),
    CAP(9, "HOLD BAR: PATTERNS OF 1, 2 OR 4 BARS"),
    HOLD(BARX, SLY, 40),
    TAP(BARX, SLY),  // bar 2 on screen: vary it
    TAP(GX(14), DY(1)), TAP(GX(15), DY(1)), TAP(GX(13), DY(3)),
    WAIT(90),
    TAP(BARX, SLY),

    CAP(10, "SYNTH: ELEVEN SOUNDS, A MASCOT EACH"),
    TAP(TABX(PAGE_SYNTH), TABY),
    TAP(TILEX(T_PLUCK), TILEY(T_PLUCK)), PRESS(KEY_DLEFT, 8), PRESS(KEY_DRIGHT, 8), PRESS(KEY_Y, 14),
    TAP(TILEX(T_BELL), TILEY(T_BELL)), PRESS(KEY_DLEFT, 14), PRESS(KEY_A, 14),
    TAP(TILEX(T_BREEZE), TILEY(T_BREEZE)), PRESS(KEY_DUP, 40),
    TAP(TILEX(T_FOLD), TILEY(T_FOLD)), PRESS(KEY_DLEFT, 14), PRESS(KEY_DRIGHT, 14),
    TAP(TILEX(T_WAVY), TILEY(T_WAVY)), PRESS(KEY_Y, 30),
    CAP(11, "DRAG THE CREATURE: ITS TWO MACROS"),
    TAP(TILEX(T_SAW), TILEY(T_SAW)),
    KEYS(KEY_L | KEY_DLEFT),
    DRAG(PADX(0.2f), PADY(0.3f), PADX(0.9f), PADY(0.8f), 60),
    DRAG(PADX(0.9f), PADY(0.8f), PADX(0.4f), PADY(0.2f), 50),
    KEYS(0),
    CAP(12, "JUNO AND DX7: SLIDE ACROSS 128 PATCHES"),
    TAP(TILEX(T_JUNO), TILEY(T_JUNO)),
    KEYS(KEY_L | KEY_DLEFT),
    DRAG(DOTX(0), DOTY(1), DOTX(9), DOTY(1), 70),
    KEYS(0),
    TAP(TILEX(T_DX7), TILEY(T_DX7)),
    PRESS(KEY_DLEFT, 14), PRESS(KEY_DRIGHT, 14),
    DRAG(DOTX(2), DOTY(3), DOTX(7), DOTY(3), 50),
    PRESS(KEY_DLEFT, 14), PRESS(KEY_Y, 20),

    CAP(13, "EDIT: DRAW THE ENVELOPE (ON WAVY)"),
    TAP(TILEX(T_WAVY), TILEY(T_WAVY)),
    TAP(EDITX, EDITY),
    PRESS(KEY_L | KEY_DLEFT, 40),
    DRAGH(0, ED_ENV_X + 50, ED_ENV_Y, 25),                                // slow attack
    PRESS(KEY_L | KEY_DLEFT, 50),
    DRAGH(1, ED_ENV_X + ED_ENV_W * 52 / 100, ED_ENV_Y + ED_ENV_H * 7 / 10, 25),  // lower sustain
    DRAGH(2, ED_ENV_X + ED_ENV_W - 6, ED_ENV_Y + ED_ENV_H, 25),            // long release
    PRESS(KEY_L | KEY_DUP, 40), WAIT(60),
    DRAGH(0, ED_ENV_X + 4, ED_ENV_Y, 20),                                 // snappy again
    CAP(14, "FILTER: CUTOFF x RESONANCE, ENV AMOUNT"),
    TAP(ETABX(EDP_FILTER), ETABY),
    KEYS(KEY_L | KEY_DLEFT),
    DRAG(EPADX(0.75f), EPADY(0.1f), EPADX(0.25f), EPADY(0.75f), 60),
    DRAG(EPADX(0.25f), EPADY(0.75f), EPADX(0.6f), EPADY(0.5f), 40),
    KEYS(0),
    DRAG(EFULLX(0.5f), ELOWY, EFULLX(0.85f), ELOWY, 25),
    PRESS(KEY_L | KEY_DDOWN, 30), PRESS(KEY_L | KEY_DLEFT, 30),
    CAP(15, "LFO: SPEED x DEPTH, FOUR TARGETS"),
    TAP(ETABX(EDP_LFO), ETABY),
    TAP(ETGTX(LFO_FILTER), ELOWY),
    KEYS(KEY_L | KEY_DLEFT),
    DRAG(EPADX(0.2f), EPADY(0.2f), EPADX(0.45f), EPADY(0.7f), 50),
    WAIT(30),
    TAP(ETGTX(LFO_PAN), ELOWY), WAIT(50),
    TAP(ETGTX(LFO_PITCH), ELOWY),
    DRAG(EPADX(0.45f), EPADY(0.7f), EPADX(0.6f), EPADY(0.25f), 30),
    WAIT(40),
    KEYS(0),
    DRAG(EPADX(0.6f), EPADY(0.25f), EPADX(0.6f), EPADY(0.0f), 15),  // depth off
    CAP(16, "UNISON x3: FATTER, ON FEWER VOICES"),
    TAP(ETABX(EDP_VOICE), ETABY),
    TAP(EUNIX(3), EROWY(1)),
    KEYS(KEY_DLEFT),
    DRAG(ESLX(0.1f), EROWY(1), ESLX(0.7f), EROWY(1), 50),
    KEYS(0),
    CAP(17, "MUTATE: NUDGE EVERYTHING A LITTLE"),
    TAP(ECHIPX, EROWY(3)), PRESS(KEY_L | KEY_DLEFT, 30),
    TAP(ECHIPX, EROWY(3)), PRESS(KEY_L | KEY_DLEFT, 30),
    CAP(18, "STORE A AND B, THEN MORPH BETWEEN"),
    TAP(EABX(0), EROWY(4)),
    TAP(ECHIPX, EROWY(3)), TAP(ECHIPX, EROWY(3)), TAP(ECHIPX, EROWY(3)),
    TAP(EABX(1), EROWY(4)),
    KEYS(KEY_L | KEY_DLEFT),
    DRAG(ESLX(1.0f), EROWY(4), ESLX(0.0f), EROWY(4), 50),
    DRAG(ESLX(0.0f), EROWY(4), ESLX(0.6f), EROWY(4), 40),
    KEYS(0),
    CAP(19, "PRESETS: SAVE THE SOUND YOU MADE"),
    CALL(tour_name_preset),
    TAP(TILEX(T_PRESET), TILEY(T_PRESET)),
    TAP(SONGBTNX, SONGBTNY(0)),  // SAVE AS
    WAIT(60),
    TAP(SONGBTNX, SONGBTNY(4)),  // BACK
    CALL(tour_name_song),
    CAP(20, "DX7 BRIGHT: HOW HARD THE FM HITS"),
    TAP(TILEX(T_DX7), TILEY(T_DX7)),
    TAP(ETABX(EDP_VOICE), ETABY),
    PRESS(KEY_DLEFT, 20),
    DRAG(EFULLX(0.5f), EROWY(2), EFULLX(0.05f), EROWY(2), 20),
    PRESS(KEY_DLEFT, 20), PRESS(KEY_DRIGHT, 20),
    DRAG(EFULLX(0.05f), EROWY(2), EFULLX(0.95f), EROWY(2), 30),
    PRESS(KEY_DLEFT, 20), PRESS(KEY_DRIGHT, 20),
    TAP(EDITX, EDITY),

    CAP(21, "DRAW: A WAVE YOU DRAW YOURSELF"),
    TAP(TILEX(T_DRAW), TILEY(T_DRAW)),
    TAP(SHAPEX(0), SHAPEY), PRESS(KEY_DLEFT, 20),
    TAP(SHAPEX(2), SHAPEY), PRESS(KEY_DLEFT, 20),
    KEYS(KEY_L | KEY_DLEFT),
    DRAG(PADX(0.0f), PADY(0.5f), PADX(0.2f), PADY(0.95f), 15),
    DRAG(PADX(0.2f), PADY(0.95f), PADX(0.45f), PADY(0.2f), 15),
    DRAG(PADX(0.45f), PADY(0.2f), PADX(0.6f), PADY(0.75f), 15),
    DRAG(PADX(0.6f), PADY(0.75f), PADX(0.8f), PADY(0.05f), 15),
    DRAG(PADX(0.8f), PADY(0.05f), PADX(1.0f), PADY(0.5f), 15),
    WAIT(20),
    KEYS(0),
    CAP(22, "PITCH BEND: SELECT + CIRCLE PAD"),
    KEYS(KEY_DLEFT), WAIT(20),
    CALL(bend_half), KEYS(KEY_DLEFT | KEY_SELECT), WAIT(12),
    CALL(bend_up), WAIT(25), CALL(bend_0), WAIT(15), CALL(bend_down), WAIT(25), CALL(bend_up), WAIT(25),
    CALL(bend_0), KEYS(KEY_DLEFT), WAIT(10), KEYS(0), WAIT(20),

    CAP(23, "ACID BASS: SQUELCH, MONO + GLIDE"),
    TAP(TILEX(T_SQUELCH), TILEY(T_SQUELCH)),
    TAP(EDITX, EDITY),
    TAP(ETABX(EDP_VOICE), ETABY),
    TAP(ECHIPX, EROWY(0)),  // MONO
    DRAG(ESLX(0.0f), EROWY(0), ESLX(0.45f), EROWY(0), 25),  // GLIDE
    PRESS(KEY_DLEFT, 14), PRESS(KEY_X, 14), PRESS(KEY_DLEFT, 14), PRESS(KEY_Y, 14),
    TAP(EDITX, EDITY),
    CAP(24, "PIANO ROLL: NOTES OF ANY LENGTH"),
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(ROLLX, SLY),
    CALL(tour_bass_rows),
    ROLL(0, 45, 2), ROLL(2, 57, 1), ROLL(3, 45, 1), ROLL(4, 48, 2), ROLL(6, 50, 1), ROLL(7, 52, 1),
    ROLL(8, 45, 2), ROLL(10, 55, 2), ROLL(12, 43, 2), ROLL(14, 45, 2),
    WAIT(60),
    CAP(25, "HOLD R + TAP A NOTE: IT SLIDES"),
    KEYS(KEY_R),
    ROLLTAP(3, 45), ROLLTAP(6, 50), ROLLTAP(10, 55),
    KEYS(0),
    WAITSEC(4),
    CAP(26, "PART 2: CHORDS ON THE ROLL"),
    TAP(PICKX(1), PICKY),
    CALL(tour_chord_rows),
    ROLL(0, 57, 8), ROLL(0, 60, 8), ROLL(0, 64, 8),
    ROLL(8, 55, 8), ROLL(8, 59, 8), ROLL(8, 62, 8),
    WAITSEC(3),

    CAP(27, "MIC SAMPLER: START, END, CHOP"),
    TAP(PICKX(2), PICKY),
    TAP(TABX(PAGE_SYNTH), TABY),
    TAP(TILEX(T_MIC), TILEY(T_MIC)),
    PRESS(KEY_DLEFT, 20), PRESS(KEY_DUP, 20),
    DRAG(GRIPX(0.0f), GRIPY, GRIPX(0.15f), GRIPY, 20),
    TAP(CHOPX, CHOPY),
    PRESS(KEY_DLEFT, 10), PRESS(KEY_DDOWN, 10), PRESS(KEY_DRIGHT, 10), PRESS(KEY_DUP, 10),
    CAP(28, "CHOPPED SLICES -> THE DRUM LANES"),
    TAP(SMP_TOKIT_X + SMP_TOKIT_W / 2, CHOPY),
    PRESS(KEY_R | KEY_DLEFT, 12), PRESS(KEY_R | KEY_DDOWN, 12), PRESS(KEY_R | KEY_DRIGHT, 12), PRESS(KEY_R | KEY_DUP, 12),

    CAP(29, "KIT: SEVEN DRUM KITS"),
    TAP(KIT_PREV_X + KIT_ARROW_W / 2, KITHEADY),  // back from the slices: the stock kits
    WAITSEC(2),
    TAP(KIT_NEXT_X + KIT_ARROW_W / 2, KITHEADY), WAITSEC(2),
    TAP(KIT_NEXT_X + KIT_ARROW_W / 2, KITHEADY), WAITSEC(2),
    TAP(KIT_NEXT_X + KIT_ARROW_W / 2, KITHEADY), WAITSEC(2),
    CAP(30, "BUILD YOUR OWN: ANY SAMPLE, TUNED"),
    TAP(KITLANEX(1), KITLANEY(1)),
    TAP(KIT_SNEXT_X + KIT_ARROW_W / 2, KITSMPY), TAP(KIT_SNEXT_X + KIT_ARROW_W / 2, KITSMPY),
    TAP(KIT_SNEXT_X + KIT_ARROW_W / 2, KITSMPY),
    DRAG(KIT_TUNE_X + KIT_SLIDER_W / 2, KITSLY, KIT_TUNE_X + KIT_SLIDER_W * 3 / 4, KITSLY, 20),
    TAP(KITLANEX(0), KITLANEY(0)),
    DRAG(KIT_LEVEL_X + KIT_SLIDER_W / 2, KITSLY, KIT_LEVEL_X + KIT_SLIDER_W * 3 / 4, KITSLY, 20),
    WAITSEC(3),
    TAP(SYN_KIT_X + SYN_EDIT_W / 2, EDITY),

    CAP(31, "FX: CRUSH"),
    TAP(TABX(PAGE_FX), TABY),
    TAP(FXSX, FXSY(FX_CRUSH)),
    DRAG(FXPX(0.2f), FXPY(0.2f), FXPX(0.9f), FXPY(0.8f), 60),
    CAP(32, "FX: SWEEP"),
    TAP(FXSX, FXSY(FX_SWEEP)),
    DRAG(FXPX(0.1f), FXPY(0.6f), FXPX(0.9f), FXPY(0.6f), 70),
    CAP(33, "FX: STUTTER"),
    TAP(FXSX, FXSY(FX_STUTTER)),
    DRAG(FXPX(0.3f), FXPY(0.3f), FXPX(0.7f), FXPY(0.7f), 60),
    CAP(34, "FX: TAPE STOP"),
    TAP(FXSX, FXSY(FX_TAPE)),
    HOLD(FXPX(0.5f), FXPY(0.5f), 60),
    WAIT(30),

    CAP(35, "MIX: DRIVE, CRUSH, TONE, PAN, MUTE, SOLO"),
    TAP(TABX(PAGE_MIX), TABY),
    DRAG(KNOBX(0, MIX_DRIVE), KNOBY(MIX_DRIVE), KNOBX(0, MIX_DRIVE), KNOBY(MIX_DRIVE) - 40, 30),
    DRAG(KNOBX(MIX_DRUMS, MIX_CRUSH), KNOBY(MIX_CRUSH), KNOBX(MIX_DRUMS, MIX_CRUSH), KNOBY(MIX_CRUSH) - 25, 30),
    DRAG(KNOBX(1, MIX_PAN), KNOBY(MIX_PAN), KNOBX(1, MIX_PAN), KNOBY(MIX_PAN) + 16, 25),
    DRAG(FADERX(2), CAPY(0.75f), FADERX(2), CAPY(0.55f), 25),
    CAP(36, "ECHO AND VERB SENDS ON EVERY PART"),
    DRAG(KNOBX(MIX_DRUMS, MIX_ECHO), KNOBY(MIX_ECHO), KNOBX(MIX_DRUMS, MIX_ECHO), KNOBY(MIX_ECHO) + 40, 30),
    DRAG(KNOBX(MIX_DRUMS, MIX_VERB), KNOBY(MIX_VERB), KNOBX(MIX_DRUMS, MIX_VERB), KNOBY(MIX_VERB) + 40, 30),
    WAIT(60),

    CAP(37, "TAPE: RECORD THE JAM, THEN SCRATCH IT"),
    TAP(TABX(PAGE_TAPE), TABY),
    TAP(TAPE_REC_X + TAPE_BTN_W / 2, TBTNY),
    TAP(TAPE_PLAY_X + TAPE_BTN_W / 2, TBTNY),
    WAITSEC(5),
    TAP(TAPE_PLAY_X + TAPE_BTN_W / 2, TBTNY),
    TAP(TAPE_REC_X + TAPE_BTN_W / 2, TBTNY),
    TAP(TAPE_REW_X + TAPE_BTN_W / 2, TBTNY),
    TAP(TAPE_PLAY_X + TAPE_BTN_W / 2, TBTNY),
    WAITSEC(2),
    DRAG(TAPE_WIN_X + 160, TWINY, TAPE_WIN_X + 60, TWINY, 20),
    DRAG(TAPE_WIN_X + 60, TWINY, TAPE_WIN_X + 150, TWINY, 20),
    DRAG(TAPE_WIN_X + 150, TWINY, TAPE_WIN_X + 70, TWINY, 20),
    WAITSEC(2),
    TAP(TAPE_PLAY_X + TAPE_BTN_W / 2, TBTNY),

    CAP(38, "HOLD SONG: ARRANGE THE PATTERNS"),
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(ROLLX, SLY),
    HOLD(SONGX, SLY, 40),
    TAP(CELLX(0), CELLY(0)), TAP(SLX(0), SLY), TAP(CELLX(0), CELLY(0)), TAP(CHIPX(0), CHIPY), TAP(CHIPX(1), CHIPY),
    TAP(CELLX(1), CELLY(1)), TAP(SLX(0), SLY), TAP(CELLX(1), CELLY(1)), TAP(CELLX(1), CELLY(1)),
    WAIT(120),

    CAP(39, "SAVE IT, THEN MIX IT DOWN TO A WAV"),
    TAP(TABX(PAGE_SOUND), TABY),
    TAP(SONGS_BTN_X + SONGS_BTN_W / 2, SONGS_BTN_Y + SONGS_BTN_H / 2),
    TAP(SONGBTNX, SONGBTNY(0)),
    WAIT(150),
    TAP(TABX(PAGE_MIX), TABY),
    TAP(MIXDOWN_X + MIXDOWN_W / 2, TABY),
    WAITSEC(4),
    TAP(TABX(PAGE_SEQ), TABY),
    WAITSEC(10),

    CAP(0, "THAT'S VIBE MACHINE. HAVE FUN!"),
    WAIT(200),
    CALL(rec_stop),
    WAIT(100000),
};
#else
static const StoryEv kStory[] = {
    CAP(0, "VIBE MACHINE: FROM NOTHING TO A FINISHED SONG"),
    CALL(story_blank),
    CALL(rec_start),
    WAIT(150),

    CAP(0, "FIRST, A KEY: A MINOR (SOUND PAGE)"),
    TAP(ROOTX(0), KEYROWY), TAP(ROOTX(0), KEYROWY), TAP(ROOTX(0), KEYROWY),
    TAP(SCALEX(1), KEYROWY),
    WAIT(60),

    CAP(1, "DRUMS: TAP THE GRID, DRAG FOR HATS"),
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(GX(0), DY(0)), TAP(GX(4), DY(0)), TAP(GX(8), DY(0)), TAP(GX(10), DY(0)), TAP(GX(12), DY(0)),
    TAP(GX(4), DY(1)), TAP(GX(12), DY(1)),
    DRAG(GX(0), DY(2), GX(15), DY(2), 70),
    TAP(PLAYX, TABY),
    WAIT(120),

    CAP(2, "BASS: PART 1 PLAYS SQUELCH"),
    TAP(TABX(PAGE_SYNTH), TABY),
    TAP(TILEX(SND_SQUELCH), TILEY(SND_SQUELCH)),
    TAP(KEYX(5), KEYY), TAP(KEYX(4), KEYY),
    CAP(2, "...ITS NOTES GO ON THE PIANO ROLL"),
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(ROLLX, SLY),
    CALL(story_scroll_bass),
    ROLL(0, 45, 3), ROLL(3, 45, 1), ROLL(6, 48, 2), ROLL(8, 43, 3), ROLL(11, 43, 1), ROLL(12, 41, 2), ROLL(14, 40, 2),
    WAIT(90),

    CAP(3, "CHORDS: PART 2, A JUNO PAD"),
    TAP(PICKX(1), PICKY),
    TAP(TABX(PAGE_SYNTH), TABY),
    TAP(TILEX(SND_JUNO), TILEY(SND_JUNO)),
    TAP(TABX(PAGE_SEQ), TABY),
    CALL(story_scroll_chords),
    ROLL(0, 57, 8), ROLL(0, 60, 8), ROLL(0, 64, 8),
    ROLL(8, 57, 4), ROLL(8, 60, 4), ROLL(8, 65, 4),
    ROLL(12, 59, 4), ROLL(12, 62, 4), ROLL(12, 67, 4),
    WAIT(90),

    CAP(4, "LEAD: PART 3, A BELL, ARPEGGIATED"),
    TAP(PICKX(2), PICKY),
    TAP(TABX(PAGE_SOUND), TABY),
    TAP(ARPX(0), ARPROWY),               // ARP on (UP, 1/16)
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(RECX, TABY),                     // record...
    KEYS(KEY_L),                         // ...an Am chord held for a bar
    HOLDMS(KEYX(0), KEYY, 2150),  // one bar at 108 BPM: the arpeggio fills it once
    KEYS(0),
    TAP(RECX, TABY),
    TAP(TABX(PAGE_SOUND), TABY),
    TAP(ARPX(0), ARPROWY),               // ARP off again
    TAP(TABX(PAGE_SEQ), TABY),
    WAIT(90),

    CAP(5, "MIX: CRUSH DRUMS, DRIVE BASS, PAN CHORDS"),
    TAP(TABX(PAGE_MIX), TABY),
    DRAG(KNOBX(MIX_DRUMS, MIX_CRUSH), KNOBY(MIX_CRUSH), KNOBX(MIX_DRUMS, MIX_CRUSH), KNOBY(MIX_CRUSH) - 28, 30),
    DRAG(KNOBX(0, MIX_DRIVE), KNOBY(MIX_DRIVE), KNOBX(0, MIX_DRIVE), KNOBY(MIX_DRIVE) - 45, 35),
    DRAG(KNOBX(1, MIX_PAN), KNOBY(MIX_PAN), KNOBX(1, MIX_PAN), KNOBY(MIX_PAN) + 16, 25),
    DRAG(FADERX(1), CAPY(0.75f), FADERX(1), CAPY(0.62f), 25),
    DRAG(KNOBX(2, MIX_TONE), KNOBY(MIX_TONE), KNOBX(2, MIX_TONE), KNOBY(MIX_TONE) - 20, 25),
    WAIT(120),

    CAP(6, "VARIATION: HOLD B TO COPY, THEN CHANGE IT"),
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(ROLLX, SLY),  // back to the grid
    HOLD(SLX(1), SLY, 50),
    WAITSEC(3),  // playing, the switch to B waits for the bar line
    TAP(GX(14), DY(0)), TAP(GX(15), DY(1)), TAP(GX(7), DY(1)),
    TAP(GX(14), DY(3)),
    WAIT(90),

    CAP(7, "RECORD A FILTER SWEEP INTO PATTERN B"),
    TAP(RECX, TABY),
    TAP(TABX(PAGE_FX), TABY),
    TAP(FXSX, FXSY(FX_SWEEP)),
    DRAG(FX_PAD_X + 30, FX_PAD_Y + 100, FX_PAD_X + FX_PAD_W - 20, FX_PAD_Y + 20, 250),
    TAP(TABX(PAGE_SEQ), TABY),
    TAP(RECX, TABY),
    WAIT(60),

    CAP(8, "ARRANGE: HOLD SONG, ADD ROWS"),
    HOLD(SONGX, SLY, 40),
    TAP(CELLX(0), CELLY(0)), TAP(SLX(0), SLY), TAP(CELLX(0), CELLY(0)), TAP(CHIPX(MIX_DRUMS), CHIPY),
    TAP(CELLX(1), CELLY(1)), TAP(SLX(0), SLY), TAP(CELLX(1), CELLY(1)),
    TAP(CELLX(2), CELLY(2)), TAP(SLX(1), SLY), TAP(CELLX(2), CELLY(2)), TAP(CELLX(2), CELLY(2)),
    TAP(CELLX(3), CELLY(3)), TAP(SLX(0), SLY), TAP(CELLX(3), CELLY(3)),
    TAP(CELLX(4), CELLY(4)), TAP(SLX(1), SLY), TAP(CELLX(4), CELLY(4)),
    WAIT(150),

    CAP(9, "SAVE IT: SONGS, SAVE AS"),
    TAP(TABX(PAGE_SOUND), TABY),
    TAP(SONGS_BTN_X + SONGS_BTN_W / 2, SONGS_BTN_Y + SONGS_BTN_H / 2),
    TAP(SONG_BTN_X + SONG_BTN_W / 2, SONG_BTN_Y0 + SONG_BTN_H / 2),
    WAIT(200),

    CAP(10, "MIXDOWN: THE WHOLE SONG TO A WAV"),
    TAP(TABX(PAGE_MIX), TABY),
    TAP(MIXDOWN_X + MIXDOWN_W / 2, TABY),
    WAITSEC(4),
    TAP(TABX(PAGE_SEQ), TABY),  // watch the song go by on the SONG sheet...
    WAITSEC(13),
    TAP(TABX(PAGE_FX), TABY),   // ...and the filter sweep play itself in B
    WAITSEC(14),

    CAP(0, "DONE: mixes/MY FIRST SONG.wav"),
    WAIT(240),
    CALL(rec_stop),
    WAIT(100000),
};
#endif

#define STORY_MOVE 14  // frames the stylus takes to get somewhere
#define STORY_GAP 10   // frames between gestures

static int s_story_i, s_story_t;
static u32 s_story_keys, s_story_keys_was;  // buttons the script is holding
static float s_story_x = 160, s_story_y = 120;

// Fills in this frame's input from the script. Returns nothing: it just
// replaces what the hardware said.
static void story_touch(u32 *down, u32 *held, u32 *up, touchPosition *tp);
static void story_input(u32 *down, u32 *held, u32 *up, touchPosition *tp) {
    *down = s_story_keys & ~s_story_keys_was;  // buttons first; the stylus adds KEY_TOUCH below
    *held = s_story_keys;
    *up = s_story_keys_was & ~s_story_keys;
    s_story_keys_was = s_story_keys;
    story_touch(down, held, up, tp);
}

static void story_touch(u32 *down, u32 *held, u32 *up, touchPosition *tp) {
    while (s_story_i < (int)(sizeof(kStory) / sizeof(kStory[0]))) {
        const StoryEv *e = &kStory[s_story_i];
        if (e->k == S_CAP) {
            s_app.caption = e->text;
            s_app.caption_n = e->a;
            s_story_i++;
            continue;
        }
        if (e->k == S_CALL) {
            e->fn();
            s_story_i++;
            continue;
        }
        if (e->k == S_KEYS) {
            s_story_keys = (u32)e->a;
            s_story_i++;
            continue;
        }
        if (e->k == S_WAIT) {
            if (++s_story_t >= e->n) {
                s_story_i++;
                s_story_t = 0;
            }
            return;
        }
        if (e->k == S_WAITSEC) {
            static uint64_t until;
            if (s_story_t++ == 0) until = svcGetSystemTick() + (uint64_t)e->n * SYSCLOCK_ARM11;
            if (svcGetSystemTick() >= until) {
                s_story_i++;
                s_story_t = 0;
            }
            return;
        }
        // A gesture: glide there, press, (drag / hold), lift, pause.
        int x = e->a, y = e->b, x2 = e->a, y2 = e->b, press = e->n;
        if (e->k == S_DRAG) {
            x2 = e->c;
            y2 = e->d;
        } else if (e->k == S_DRAGH) {  // from wherever that handle is now
            static int hx0, hy0;
            if (s_story_t == 0) {
                float hx[3], hy[3];
                ui_env_handles(part_edit()->cur.p, hx, hy);
                hx0 = (int)hx[e->a];
                hy0 = (int)hy[e->a];
            }
            x = hx0;
            y = hy0;
            x2 = e->c;
            y2 = e->d;
        } else if (e->k == S_ROLL && e->c == 0) {  // just a tap on the note there
            const int row = music_roll_row(e->b, s_app.roll_chromatic) - s_app.roll_scroll;
            x = x2 = ROLL_X0 + e->a * ROLL_CELL_W + ROLL_CELL_W / 2;
            y = y2 = ROLL_Y0 + (ROLL_ROWS - 1 - row) * ROLL_ROW_H + ROLL_ROW_H / 2;
            press = 5;
        } else if (e->k == S_ROLL) {  // a note on the piano roll, stretched to its length
            const int row = music_roll_row(e->b, s_app.roll_chromatic) - s_app.roll_scroll;
            x = ROLL_X0 + e->a * ROLL_CELL_W + ROLL_CELL_W / 2;
            y = ROLL_Y0 + (ROLL_ROWS - 1 - row) * ROLL_ROW_H + ROLL_ROW_H / 2;
            // Length 1 after a longer note: a nudge left sets it (new notes
            // take the last length you stretched to).
            x2 = e->c > 1 ? x + (e->c - 1) * ROLL_CELL_W : x - ROLL_CELL_W;
            y2 = y;
            press = 6 + e->c * 3;
        }
        const int t = s_story_t++;
        const float sx = s_story_x, sy = s_story_y;
        s_app.cursor_on = true;
        if (t < STORY_MOVE) {  // gliding over
            const float f = (float)(t + 1) / STORY_MOVE, ease = f * f * (3 - 2 * f);
            s_app.cursor_x = sx + (x - sx) * ease;
            s_app.cursor_y = sy + (y - sy) * ease;
            s_app.cursor_down = false;
            if (t == STORY_MOVE - 1) {
                s_story_x = x;
                s_story_y = y;
            }
            return;
        }
        const int p = t - STORY_MOVE;
        if (e->k == S_HOLD && e->c > 0) {  // a timed hold: down until the time is up
            static uint64_t until;
            static int end;  // the frame it ended on, fixed once it has (-1 while held)
            if (p == 0) {
                until = svcGetSystemTick() + (uint64_t)e->c * (SYSCLOCK_ARM11 / 1000);
                end = -1;
            }
            if (end < 0 && p > 0 && svcGetSystemTick() >= until) end = p - 1;
            press = end < 0 ? p + 1 : end;
        }
        if (p <= press) {  // stylus down (dragging along, if it's a drag)
            const float f = press > 0 ? (float)p / press : 1.0f;
            const float cx = x + (x2 - x) * f, cy = y + (y2 - y) * f;
            *held |= KEY_TOUCH;
            if (p == 0) *down |= KEY_TOUCH;
            tp->px = (u16)cx;
            tp->py = (u16)cy;
            s_app.cursor_x = cx;
            s_app.cursor_y = cy;
            s_app.cursor_down = true;
            return;
        }
        if (p == press + 1) {  // lift
            *up |= KEY_TOUCH;
            s_story_x = x2;
            s_story_y = y2;
            s_app.cursor_down = false;
            return;
        }
        if (p < press + 1 + STORY_GAP) return;
        s_story_i++;
        s_story_t = 0;
    }
}
#endif

int main(void) {
    gfxInitDefault();
    gfxSet3D(true);  // the top-screen scene has real depth; the slider scales it
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(16384);  // the pattern engine draws a lot of small triangles
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *top_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    ui_init();

    for (int s = 0; s < NUM_SOURCES; s++) s_held[s].key = -1;
    s_app.patch = 0;
    s_app.active_slider = -1;
    s_app.page = PAGE_SOUND;
    for (int k = 0; k < FX_COUNT; k++) s_app.fx_x[k] = s_app.fx_y[k] = 0.5f;

    romfsInit();  // the drum kits' samples live in the app's romfs
    engine_init();
    drum_kit_stock(&s_app.kit, 0);
    s_app.kit_lane = 0;
    seq_init(seq_out);
    // Buttons and the touchscreen are read once a frame: on average a press
    // happened half a frame before we see it. Live recording allows for it.
    seq_set_input_delay(engine_sample_rate() / 120);
    s_app.slot_hold = -1;
    s_app.part_hold = -1;
    s_app.roll_active = -1;
    s_app.roll_scroll = music_roll_row(57, false);  // A3 at the bottom: middle C sits low on the roll
    const float initial[PARAM_COUNT] = {0.5f, 0.15f, 0.25f, 0.4f};
    for (int i = 0; i < PARAM_COUNT; i++) s_app.params[i] = initial[i];
    default_parts();
    s_app.mix_strip = -1;
    s_app.mix_grab = MIX_GRAB_NONE;
    mix_default();
    s_app.arr_sel = -1;
    s_app.song_btn_hold = -1;
    s_app.bar_hold = -1;
    set_key(0, 0);
    set_arp(&(SeqArp){false, ARP_UP, ARP_16TH, 1});
    sampler_edit_default(&s_app.sample_edit);
    mkdir("sdmc:/3ds", 0777);
    mkdir(STORAGE_DIR, 0777);
    {
        SaveState st;
        if (storage_load(STORAGE_PATH, &st)) {
            restore(&st);
#ifndef VM_DEMO
            load_lanes(STORAGE_DIR);  // the drum lanes' own samples
#endif
            s_restored_unsaved = st.unsaved;
            s_restored_tape_changed = st.tape_changed;
            s_restored_mic_changed = st.mic_changed;
        }
        load_boing();  // the MIC placeholder, unless a saved take replaces it
#ifndef VM_DEMO
        int rate = 0, frames = wav_read(STORAGE_DIR "/mic.wav", s_take_buf, SAMPLER_MAX_FRAMES, &rate);
        if (frames > 0 && rate == SAMPLER_RATE && engine_load_sample(s_take_buf, frames, rate, &s_app.sample_edit)) {
            show_take(s_take_buf, frames);
            s_take_user = true;
        }
#endif
        s_app.seq = seq_status();
        seq_snapshot(&s_app.pattern);
        capture(&s_saved);  // what is on disk now
        s_seen = s_saved;
    }
    s_app.tape_link = true;
    s_app.tape_hold_track = -1;
    tape_init();  // before the audio thread starts using it
#ifndef VM_DEMO
    load_tape();  // needs the tape memory from tape_init
#endif
    s_app.audio_ok = audio_out_init();
    s_app.mic_ok = mic_init();
    mkdir(SONGS_ROOT, 0777);
    s_app.song_hold = -1;
    song_mark_clean();  // what was restored matches the song's folder...
    s_force_dirty = s_restored_unsaved;  // ...unless the autosave says it didn't
    for (int t = 0; t < TAPE_TRACKS; t++)  // tracks changed before the restart stay "changed"
        if ((s_restored_tape_changed >> t) & 1u) s_song_track_gen[t] = ~tape_track_generation(t);
    if (s_restored_mic_changed) s_song_mic_gen = ~s_mic_gen;
    s_app.song_dirty = s_force_dirty;
    refresh_songs();

    hidSetRepeatParameters(18, 5);
    u32 bg = C2D_Color32(0x00, 0x00, 0x00, 0xFF);
    int16_t scope[SCOPE_POINTS];

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp(), repeat = hidKeysDownRepeat();
        hidTouchRead(&s_tp);
#ifdef VM_STORY
        story_input(&down, &held, &up, &s_tp);  // the script plays the 3DS
        repeat = 0;
#endif
        s_keys_held = held;
        // (Not on the press frame itself: s_start_down is only set below.)
        if ((held & KEY_START) && !(down & KEY_START) && s_app.frame - s_start_down >= QUIT_HOLD_FRAMES) break;

        s_app.seq = seq_status();
        seq_snapshot(&s_app.pattern);
        seq_get_arrangement(&s_app.arr);
        {   // the bar on screen: within the pattern; following the playhead while recording live
            const int bars = seq_bars(&s_app.pattern);
            if (s_app.seq.playing && s_app.seq.recording) s_app.bar_page = s_app.seq.step / SEQ_STEPS;
            if (s_app.bar_page >= bars) s_app.bar_page = bars - 1;
            if (s_app.bar_page < 0) s_app.bar_page = 0;
        }
        fx_set_tempo((float)s_app.seq.bpm);
        s_app.fx = fx_state();
        s_app.tape = tape_status();
        {   // a take ran to the end of the tape: it stopped there
            static uint32_t ends;
            if (s_app.tape.ends != ends) {
                if (ends != 0 || s_app.frame > 1) {
                    s_tape_armed = false;
                    tape_record(false);
                    if (seq_status().playing && s_app.tape_link) seq_toggle_play();
                    notice("END OF THE TAPE: TAKE STOPPED");
                }
                ends = s_app.tape.ends;
            }
        }
        watch_takes();
        ui_tape_ui_state(s_tape_armed, s_tape_speed);
        s_app.tape_armed = s_tape_armed;

#ifdef VM_DEMO
        // Debug attract mode (make DEMO=1): silent, starts the sequencer and
        // rotates themes, flipping to the SEQ page half the time.
        if (s_app.frame == 1) {
            select_sound(sound_mic_index() - 1);  // the first SYNTH visit lands on MIC
            for (int i = 1; i < 4; i++) {  // a little song: A, then three rolled variations
                SeqPattern pat;
                dice_groove(&pat, 777u * i, 0);
                for (int k = 0; k < pat.count; k++) pat.notes[k].part = (uint8_t)(k % PARTS);  // all three parts play
                seq_put_slot(i, &pat);
            }
            seq_set_song(true);
            seq_set_swing(0.18f);
            s_tape_armed = true;                    // and the tape records the demo
            tape_record(true);
            tape_button(TAPE_HIT_PLAY);
        }
        if (s_app.frame % 300 == 299) tape_select((int)((s_app.frame / 300) % TAPE_TRACKS));
        if (s_app.frame % 480 == 479) ui_set_theme(next_theme());
        s_app.page = (Page)((s_app.frame / 120) % (PAGE_COUNT + 1));  // + SONGS
        if (s_app.frame == 40 || s_app.frame == 50 || s_app.frame == 60) {
            char nm[SONG_NAME_MAX + 1];
            song_random_name(s_app.frame * 7919u, nm);
            save_song(nm);
        }
        if (s_app.page == PAGE_SONGS && s_app.frame % 120 == 0) refresh_songs();
        s_app.roll_view = (s_app.frame / 720) & 1;  // every other SEQ visit shows the piano roll
        if (s_app.frame % 720 == 0) switch_part((int)((s_app.frame / 720) % PARTS));
        update_song_dirty();
        if (s_app.page == PAGE_SYNTH && s_app.frame % 120 == 0) select_sound(s_app.sound + 1);
        if (s_app.page == PAGE_MIX) {  // someone at the desk: knobs turning, a fader riding
            const float ph = s_app.frame * 0.03f;
            s_app.mix[1].knob[MIX_DRIVE] = 0.55f + 0.4f * sinf(ph);
            s_app.mix[MIX_DRUMS].knob[MIX_CRUSH] = 0.35f;
            s_app.mix[0].knob[MIX_PAN] = 0.5f + 0.35f * sinf(ph * 0.6f);
            s_app.mix[2].knob[MIX_TONE] = 0.78f;
            s_app.mix[2].level = 0.55f + 0.15f * sinf(ph * 0.8f);
            s_app.mix_solo = 0;
            s_app.mix_strip = 1;
            s_app.mix_grab = MIX_GRAB_KNOB + MIX_DRIVE;  // shows its value
            for (int k = 0; k < MIX_STRIPS; k++) mix_apply(k);
        } else if (s_app.mix_strip >= 0) {
            s_app.mix_strip = -1;
            s_app.mix_grab = MIX_GRAB_NONE;
        }
        {   // wander a pretend stylus around the pads
            float a = s_app.frame * 0.05f;
            float px = 0.5f + 0.38f * sinf(a), py = 0.5f + 0.38f * cosf(a * 1.3f);
            s_app.pad_touch = s_app.page == PAGE_SYNTH || s_app.page == PAGE_FX;
            s_app.pad_x = px;
            s_app.pad_y = py;
            if (s_app.page == PAGE_SYNTH && sound_get(s_app.sound)->kind == SOUND_CUSTOM) {
                set_macro(0, px);
                set_macro(1, py);
            }
            // Pretend to record when the MIC sound comes round, for screenshots.
            unsigned f = s_app.frame % 120;
            s_app.mic_rec = s_app.page == PAGE_SYNTH && s_app.sound == sound_mic_index() && f > 30 && f < 100;
            s_app.mic_level = fabsf(sinf(s_app.frame * 0.4f)) * 0.8f;
            if (s_app.mic_rec) s_app.mic_secs = (f - 30) / 60.0f;
            s_app.fx_sel = (int)((s_app.frame / 480) % FX_COUNT);
            for (int k = 0; k < FX_COUNT; k++)
                if (k != s_app.fx_sel || s_app.page != PAGE_FX) fx_set((FxKind)k, false, px, py);
            if (s_app.page == PAGE_FX) fx_pad_move(px, py);
        }
#endif
        handle_buttons(down, held, up, repeat);
        handle_touch(down, held, up);
        if (s_app.notice_frames > 0) s_app.notice_frames--;
        s_app.notice_age++;
        {   // KIT: lanes light up as they're hit (by the pattern, or the keys)
            static uint32_t fired;
            if (s_app.seq.playing && s_app.seq.fired != fired) {
                fired = s_app.seq.fired;
                const int step = s_app.seq.step;
                const uint8_t hits = step >= 0 && step < SEQ_STEPS_MAX ? s_app.pattern.drums[step] : 0;
                for (int l = 0; l < DRUM_LANES; l++)
                    if ((hits >> l) & 1u) s_app.drum_flash[l] = 6;
            }
            for (int l = 0; l < DRUM_LANES; l++)
                if (s_app.drum_flash[l] > 0) s_app.drum_flash[l]--;
        }
#ifndef VM_DEMO
        if (s_app.frame % 30 == 0) {
            update_song_dirty();  // first, so the autosave's "unsaved" is current
            autosave(false);
        }
        if (s_app.page == PAGE_SONGS) {  // read tempo / patterns for visible rows, two a frame
            int filled = 0;
            for (int r = 0; r < SONG_ROWS && filled < 2; r++) {
                int i = s_app.song_scroll + r;
                if (i < s_app.song_count && s_app.songs[i].bpm < 0) {
                    if (!song_info(SONGS_ROOT, &s_app.songs[i])) s_app.songs[i].bpm = 0;
                    filled++;
                }
            }
        }
        {   // the tape just stopped: a good moment to write its tracks
            static bool was_playing;
            if (was_playing && !s_app.tape.playing) save_tape();
            was_playing = s_app.tape.playing;
        }
#endif
        follow_sequencer();
        audition_tick();
        if (s_app.mic_rec) {
            bool room = mic_poll();
            s_app.mic_level = mic_level();
            s_app.mic_secs = (float)mic_frames() / SAMPLER_RATE;
            if (!room) finish_recording();  // the take is full: stop by itself
        }
        update_lit_keys();
        mix_update();
        if (s_app.smp_flash_frames > 0) s_app.smp_flash_frames--;
        fx_automation();
        mixdown_tick();

        AudioStats stats = audio_out_stats();
#ifdef VM_STORY
        {   // perf log: once a second, the CPU meter and the gap count
            static FILE *pf;
            if (!pf) pf = fopen(STORAGE_DIR "/perf.txt", "w");
            if (pf && s_app.frame % 60 == 0) {
                fprintf(pf, "%u page %d load %.3f peak %.3f late %u\n", (unsigned)s_app.frame, (int)s_app.page,
                        (double)stats.load, (double)stats.load_peak, (unsigned)stats.late);
                fflush(pf);
            }
        }
#endif
        audio_out_scope(scope);

        ui_update(&s_app, &stats);
        float depth = osGet3DSliderState();

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
#ifdef VM_RECORD
        rec_frame();
#endif
        C2D_TargetClear(top, bg);
        C2D_SceneBegin(top);
        ui_draw_top(&s_app, &stats, scope, -depth);
        if (depth > 0.0f) {
            C2D_TargetClear(top_right, bg);
            C2D_SceneBegin(top_right);
            ui_draw_top(&s_app, &stats, scope, depth);
        }
        C2D_TargetClear(bottom, bg);
        C2D_SceneBegin(bottom);
        ui_draw_bottom(&s_app);
        C3D_FrameEnd(0);
        run_job();  // after the frame that shows "SAVING..." has gone out
        s_app.frame++;
    }

    if (s_app.mixdown) mixdown_finish();  // quitting mid-mixdown: keep what was recorded
#ifndef VM_DEMO
    update_song_dirty();
    autosave(true);
    tape_play(false);
    save_tape();
#else
    (void)save_tape;
    (void)load_tape;
    (void)autosave;  // the attract build never touches the SD card
#endif
    if (s_app.seq.playing) seq_toggle_play();
    release_all();
    mic_exit();
    audio_out_exit();
    tape_exit();
    engine_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
