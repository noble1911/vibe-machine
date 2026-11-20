// Vibe Machine -- a pocket synth / groovebox for the Nintendo 3DS.
//
// main.c owns the frame loop and turns buttons and touches into engine and
// sequencer calls. Sound is rendered on a separate thread (audio_out.c),
// which also runs the sequencer clock (seq.c); drawing lives in ui*.c.

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "audio_out.h"
#include "engine.h"
#include "music.h"
#include "patch_names.h"
#include "dice.h"
#include "mic.h"
#include "sampler.h"
#include "seq.h"
#include "storage.h"
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
} TouchMode;

static Held s_held[NUM_SOURCES];
static AppState s_app;
static int s_theme;

static TouchMode s_touch;
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

static void source_release(int src) {
    Held *h = &s_held[src];
    if (h->key < 0) return;
    for (int i = 0; i < h->count; i++) engine_note_off(h->synth, h->notes[i]);
    h->key = -1;
    h->count = 0;
}

static void source_press(int src, int key) {
    source_release(src);
    Held *h = &s_held[src];
    h->key = key;
    h->synth = s_app.mode == PLAY_DRUM ? SYNTH_DRUMS : SYNTH_LEAD;
    h->count = music_key_notes(key, s_app.mode, s_app.octave, h->notes);
    float velocity = s_app.mode == PLAY_DRUM ? 1.0f : 0.8f;
    for (int i = 0; i < h->count; i++) engine_note_on(h->synth, h->notes[i], velocity);
    ui_key_struck(key, s_app.mode);

    // Remember it for painting into the grid, and record it if armed.
    if (s_app.mode == PLAY_DRUM) {
        seq_record_drum(key);
    } else {
        SeqNote n = {(int8_t)key, (uint8_t)h->count, {0}};
        for (int i = 0; i < h->count; i++) n.notes[i] = (uint8_t)h->notes[i];
        s_last_note = n;
        seq_record_synth(&n);
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
    engine_set_macro(s_app.sound, m, v);
}

static void select_sound(int index) {
    int n = sound_count();
    index = ((index % n) + n) % n;
    const Sound *snd = sound_get(index);
    release_all();
    s_app.sound = index;
    if (snd->kind == SOUND_BANK) s_app.patch = snd->bank_first;
    engine_load_sound(index, s_app.patch);
    for (int m = 0; m < SOUND_MACROS; m++) {
        s_app.macro[m] = snd->macro_default[m];
        engine_set_macro(index, m, s_app.macro[m]);
    }
    reapply_params();
}

// Step through the current BANK sound's patches (wrapping inside the bank).
static void change_patch(int patch) {
    const Sound *snd = sound_get(s_app.sound);
    if (snd->kind != SOUND_BANK) return;
    int first = snd->bank_first, n = snd->bank_count;
    patch = first + (((patch - first) % n) + n) % n;
    release_all();
    s_app.patch = patch;
    engine_load_sound(s_app.sound, patch);
    reapply_params();
}

// ---------------------------------------------------------------- MIC ----

static float s_take_secs;  // length of the loaded take

static void show_take(const int16_t *data, int frames) {
    sampler_overview(data, frames, s_app.take_overview, SAMPLER_OVERVIEW);
    s_take_secs = s_app.mic_secs = (float)frames / SAMPLER_RATE;
}

static void finish_recording(void) {
    int frames = 0;
    int16_t *take = mic_stop(&frames);
    s_app.mic_rec = false;
    frames = sampler_tidy(take, frames);
    if (frames > SAMPLER_RATE / 20 && engine_load_sample(take, frames, SAMPLER_RATE)) {
        show_take(take, frames);
        set_macro(0, 0.0f);  // play the new take from its start
        engine_set_macro(s_app.sound, 0, 0.0f);
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

static void capture(SaveState *st) {
    memset(st, 0, sizeof(*st));  // zero padding too, so states compare with memcmp
    st->theme = s_theme;
    st->sound = s_app.sound;
    st->patch = s_app.patch;
    memcpy(st->macro, s_app.macro, sizeof(st->macro));
    memcpy(st->params, s_app.params, sizeof(st->params));
    st->octave = s_app.octave;
    st->bpm = s_app.seq.bpm;
    st->pattern = s_app.pattern;
    memcpy(st->fx_x, s_app.fx_x, sizeof(st->fx_x));
    memcpy(st->fx_y, s_app.fx_y, sizeof(st->fx_y));
}

static void restore(const SaveState *st) {
    s_theme = ((st->theme % ui_theme_count()) + ui_theme_count()) % ui_theme_count();
    ui_set_theme(s_theme);
    for (int i = 0; i < PARAM_COUNT; i++) s_app.params[i] = st->params[i];
    select_sound(st->sound);
    const Sound *snd = sound_get(s_app.sound);
    if (snd->kind == SOUND_BANK && st->patch >= snd->bank_first && st->patch < snd->bank_first + snd->bank_count)
        change_patch(st->patch);
    for (int m = 0; m < SOUND_MACROS; m++) {
        s_app.macro[m] = st->macro[m];
        engine_set_macro(s_app.sound, m, st->macro[m]);
    }
    s_app.octave = st->octave < MUSIC_OCTAVE_MIN || st->octave > MUSIC_OCTAVE_MAX ? 0 : st->octave;
    seq_set_bpm(st->bpm);
    seq_set_pattern(&st->pattern);
    memcpy(s_app.fx_x, st->fx_x, sizeof(s_app.fx_x));
    memcpy(s_app.fx_y, st->fx_y, sizeof(s_app.fx_y));
}

// Save a moment after things stop changing (and again on quit). The file
// is tiny, so this is cheap even on the 3DS's SD card.
#define SAVE_SETTLE_FRAMES 90
static SaveState s_saved, s_seen;
static unsigned s_changed_at;

static void autosave(bool force) {
    SaveState now;
    capture(&now);
    if (memcmp(&now, &s_seen, sizeof(now)) != 0) {
        s_seen = now;
        s_changed_at = s_app.frame;
    }
    bool dirty = memcmp(&now, &s_saved, sizeof(now)) != 0;
    if (dirty && (force || s_app.frame - s_changed_at >= SAVE_SETTLE_FRAMES)) {
        if (storage_save(STORAGE_PATH, &now)) s_saved = now;
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

static bool cell_on(int step, int row) {
    if (row == 0) return s_app.pattern.synth[step].count > 0;
    return (s_app.pattern.drums[step] >> (row - 1)) & 1u;
}

static void paint_cell(int step, int row) {
    if (row == 0)
        seq_set_synth(step, s_paint_on ? &s_last_note : NULL);
    else
        seq_set_drum(step, row - 1, s_paint_on);
    s_paint_step = step;
}

static void press_button(SeqButton b) {
    switch (b) {
        case SEQ_BTN_PLAY: seq_toggle_play(); break;
        case SEQ_BTN_REC: seq_set_recording(!s_app.seq.recording); break;
        case SEQ_BTN_BPM_DOWN: seq_set_bpm(s_app.seq.bpm - 1); break;
        case SEQ_BTN_BPM_UP: seq_set_bpm(s_app.seq.bpm + 1); break;
        case SEQ_BTN_CLEAR: seq_clear(); break;
        case SEQ_BTN_DICE: {
            SeqPattern p;
            dice_groove(&p, (uint32_t)svcGetSystemTick() ^ (s_app.frame * 2654435761u), s_app.octave);
            seq_set_pattern(&p);
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
    for (int lane = 0; lane < SEQ_LANES; lane++)
        if ((s_app.pattern.drums[step] >> lane) & 1u) {
            ui_seq_struck(lane, true);
            s_flash[lane] = FLASH_FRAMES;
        }
    const SeqNote *n = &s_app.pattern.synth[step];
    if (n->count && n->key >= 0 && n->key < MUSIC_KEYS) {
        ui_seq_struck(n->key, false);
        s_flash[n->key] = FLASH_FRAMES;
    }
}

// --------------------------------------------------------------- input ----

static void touch_begin(int x, int y) {
    int tab = ui_hit_tab(x, y);
    if (tab >= 0) {
        s_app.page = (Page)tab;
        s_touch = TOUCH_IGNORE;
        return;
    }
    if (ui_hit_key(x, y) >= 0) {
        s_touch = TOUCH_KEYS;
        return;
    }
    if (s_app.page == PAGE_SYNTH && s_app.sound == sound_mic_index() && ui_hit_synth_rec(x, y)) {
        if (s_app.mic_ok) {
            release_all();
            mic_start();
            s_app.mic_rec = mic_recording();
        }
        s_touch = TOUCH_REC;
        return;
    }
    if (s_app.page == PAGE_SYNTH && ui_hit_synth_dice(x, y)) {
        roll_sound();
        s_touch = TOUCH_IGNORE;
        return;
    }
    if (s_app.page == PAGE_SYNTH) {
        int tile = ui_hit_sound_tile(x, y);
        if (tile >= 0) {
            select_sound(tile);
            s_touch = TOUCH_IGNORE;
        } else if (ui_in_synth_pad(x, y)) {
            if (sound_get(s_app.sound)->kind == SOUND_BANK) {
                s_touch = TOUCH_BANK;
                s_paint_step = -1;
            } else {
                s_touch = TOUCH_SYNTH_PAD;
            }
        } else {
            s_touch = TOUCH_IGNORE;
        }
        return;
    }
    if (s_app.page == PAGE_FX) {
        int hit = ui_hit_fx_sticker(x, y);
        if (hit >= 0) {
            fx_sticker(hit);
            s_touch = TOUCH_IGNORE;
        } else {
            s_touch = ui_in_fx_pad(x, y) ? TOUCH_FX_PAD : TOUCH_IGNORE;
        }
        return;
    }
    if (s_app.page == PAGE_SOUND) {
        if (ui_hit_theme(x, y)) {
            ui_set_theme(next_theme());
            s_touch = TOUCH_IGNORE;
            return;
        }
        s_app.active_slider = ui_hit_slider(x, y);
        s_touch = s_app.active_slider >= 0 ? TOUCH_SLIDER : TOUCH_IGNORE;
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
        if (s_touch == TOUCH_REC && s_app.mic_rec) finish_recording();
        s_app.pad_touch = false;
        source_release(SOURCE_TOUCH);
        s_app.active_slider = -1;
        s_touch = TOUCH_NONE;
        return;
    }
    if (!(held & KEY_TOUCH)) return;

    touchPosition tp;
    hidTouchRead(&tp);
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
        case TOUCH_BUTTON:
            s_button_frames++;
            if ((s_button == SEQ_BTN_BPM_DOWN || s_button == SEQ_BTN_BPM_UP) && s_button_frames > REPEAT_DELAY &&
                s_button_frames % REPEAT_EVERY == 0)
                press_button(s_button);
            break;
        default: break;
    }
}

static void handle_buttons(u32 down, u32 held, u32 up, u32 repeat) {
    s_app.mode = (held & KEY_L) ? PLAY_CHORD : (held & KEY_R) ? PLAY_DRUM : PLAY_NOTE;

    for (int k = 0; k < MUSIC_KEYS; k++) {
        if (down & kKeyButtons[k]) source_press(k, k);
        if (up & kKeyButtons[k]) source_release(k);
    }

    // Circle pad left/right: next patch inside a bank, otherwise next sound.
    bool bank = sound_get(s_app.sound)->kind == SOUND_BANK;
    if (repeat & KEY_CPAD_RIGHT) bank ? change_patch(s_app.patch + 1) : select_sound(s_app.sound + 1);
    if (repeat & KEY_CPAD_LEFT) bank ? change_patch(s_app.patch - 1) : select_sound(s_app.sound - 1);
    if ((down & KEY_CPAD_UP) && s_app.octave < MUSIC_OCTAVE_MAX) s_app.octave++;
    if ((down & KEY_CPAD_DOWN) && s_app.octave > MUSIC_OCTAVE_MIN) s_app.octave--;
    if (down & KEY_SELECT) select_sound(s_app.sound + 1);

    // START: tap to play/stop; holding it quits (checked in the main loop).
    if (down & KEY_START) s_start_down = s_app.frame;
    if ((up & KEY_START) && s_app.frame - s_start_down < PLAY_TAP_FRAMES) seq_toggle_play();
}

static void update_lit_keys(void) {
    memset(s_app.key_lit, 0, sizeof(s_app.key_lit));
    for (int s = 0; s < NUM_SOURCES; s++)
        if (s_held[s].key >= 0) s_app.key_lit[s_held[s].key] = true;
    for (int k = 0; k < MUSIC_KEYS; k++)
        if (s_flash[k] > 0) {
            s_app.key_lit[k] = true;
            s_flash[k]--;
        }
}

// ---------------------------------------------------------------- main ----

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

    engine_init();
    seq_init();
    const float initial[PARAM_COUNT] = {0.5f, 0.15f, 0.25f, 0.4f};
    for (int i = 0; i < PARAM_COUNT; i++) s_app.params[i] = initial[i];
    select_sound(0);
    mkdir("sdmc:/3ds", 0777);
    mkdir(STORAGE_DIR, 0777);
    {
        SaveState st;
        if (storage_load(STORAGE_PATH, &st)) restore(&st);
        s_app.seq = seq_status();
        seq_snapshot(&s_app.pattern);
        capture(&s_saved);  // what is on disk now
        s_seen = s_saved;
    }
    s_app.audio_ok = audio_out_init();
    s_app.mic_ok = mic_init();
    {   // the MIC sound starts with the placeholder engine_init loaded
        static int16_t boing[SAMPLER_RATE];
        show_take(boing, sampler_boing(boing, SAMPLER_RATE));
    }

    hidSetRepeatParameters(18, 5);
    u32 bg = C2D_Color32(0x00, 0x00, 0x00, 0xFF);
    int16_t scope[SCOPE_POINTS];

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp(), repeat = hidKeysDownRepeat();
        // (Not on the press frame itself: s_start_down is only set below.)
        if ((held & KEY_START) && !(down & KEY_START) && s_app.frame - s_start_down >= QUIT_HOLD_FRAMES) break;

        s_app.seq = seq_status();
        seq_snapshot(&s_app.pattern);
        fx_set_tempo((float)s_app.seq.bpm);
        s_app.fx = fx_state();

#ifdef VM_DEMO
        // Debug attract mode (make DEMO=1): silent, starts the sequencer and
        // rotates themes, flipping to the SEQ page half the time.
        if (s_app.frame == 1) {
            seq_toggle_play();
            select_sound(sound_mic_index() - 1);  // the first SYNTH visit lands on MIC
        }
        if (s_app.frame % 480 == 479) ui_set_theme(next_theme());
        s_app.page = (Page)((s_app.frame / 120) % PAGE_COUNT);
        if (s_app.page == PAGE_SYNTH && s_app.frame % 120 == 0) select_sound(s_app.sound + 1);
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
#ifndef VM_DEMO
        if (s_app.frame % 30 == 0) autosave(false);
#endif
        follow_sequencer();
        if (s_app.mic_rec) {
            bool room = mic_poll();
            s_app.mic_level = mic_level();
            s_app.mic_secs = (float)mic_frames() / SAMPLER_RATE;
            if (!room) finish_recording();  // the take is full: stop by itself
        }
        update_lit_keys();

        AudioStats stats = audio_out_stats();
        audio_out_scope(scope);

        ui_update(&s_app, &stats);
        float depth = osGet3DSliderState();

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
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
        s_app.frame++;
    }

#ifndef VM_DEMO
    autosave(true);
#else
    (void)autosave;  // the attract build never touches the SD card
#endif
    if (s_app.seq.playing) seq_toggle_play();
    release_all();
    mic_exit();
    audio_out_exit();
    engine_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
