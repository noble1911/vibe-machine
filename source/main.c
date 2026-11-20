// Vibe Machine -- a pocket synth / groovebox for the Nintendo 3DS.
//
// main.c owns the frame loop and turns buttons and touches into engine and
// sequencer calls. Sound is rendered on a separate thread (audio_out.c),
// which also runs the sequencer clock (seq.c); drawing lives in ui*.c.

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "audio_out.h"
#include "engine.h"
#include "music.h"
#include "patch_names.h"
#include "seq.h"
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
typedef enum { TOUCH_NONE, TOUCH_IGNORE, TOUCH_SLIDER, TOUCH_KEYS, TOUCH_CELLS, TOUCH_BUTTON } TouchMode;

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

static void change_patch(int patch) {
    if (patch < PATCH_FIRST) patch = PATCH_LAST;
    if (patch > PATCH_LAST) patch = PATCH_FIRST;
    release_all();
    s_app.patch = patch;
    engine_set_patch(patch);
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

    if (repeat & KEY_CPAD_RIGHT) change_patch(s_app.patch + 1);
    if (repeat & KEY_CPAD_LEFT) change_patch(s_app.patch - 1);
    if ((down & KEY_CPAD_UP) && s_app.octave < MUSIC_OCTAVE_MAX) s_app.octave++;
    if ((down & KEY_CPAD_DOWN) && s_app.octave > MUSIC_OCTAVE_MIN) s_app.octave--;
    if (down & KEY_SELECT) change_patch(s_app.patch < 128 ? 128 : 0);

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
    C2D_Init(8192);  // the pattern engine draws a lot of small triangles
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *top_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    ui_init();

    for (int s = 0; s < NUM_SOURCES; s++) s_held[s].key = -1;
    s_app.patch = 0;
    s_app.active_slider = -1;
    s_app.page = PAGE_SOUND;

    engine_init();
    seq_init();
    const float initial[PARAM_COUNT] = {0.5f, 0.15f, 0.25f, 0.4f};
    for (int i = 0; i < PARAM_COUNT; i++) {
        s_app.params[i] = initial[i];
        engine_set_param((EngineParam)i, initial[i]);
    }
    s_app.audio_ok = audio_out_init();

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

#ifdef VM_DEMO
        // Debug attract mode (make DEMO=1): silent, starts the sequencer and
        // rotates themes, flipping to the SEQ page half the time.
        if (s_app.frame == 1) {
            engine_set_param(PARAM_VOLUME, 0.0f);
            seq_toggle_play();
        }
        if (s_app.frame % 240 == 239) ui_set_theme(next_theme());
        s_app.page = (s_app.frame / 120) & 1 ? PAGE_SEQ : PAGE_SOUND;
#endif
        handle_buttons(down, held, up, repeat);
        handle_touch(down, held, up);
        follow_sequencer();
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

    if (s_app.seq.playing) seq_toggle_play();
    release_all();
    audio_out_exit();
    engine_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
