// Vibe Machine -- a pocket synth / groovebox for the Nintendo 3DS.
//
// main.c owns the frame loop and turns buttons and touches into engine
// calls. Sound is rendered on a separate thread (audio_out.c); drawing lives
// in ui.c.

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "audio_out.h"
#include "engine.h"
#include "music.h"
#include "patch_names.h"
#include "ui.h"

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

static Held s_held[NUM_SOURCES];
static AppState s_app;
static bool s_touch_consumed;
static int s_theme;

static int ui_theme_index_next(void) {
    s_theme = (s_theme + 1) % ui_theme_count();
    return s_theme;
}

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

static void handle_touch(u32 down, u32 held, u32 up) {
    if (up & KEY_TOUCH) {
        source_release(SOURCE_TOUCH);
        s_app.active_slider = -1;
        return;
    }
    if (!(held & KEY_TOUCH)) return;

    touchPosition tp;
    hidTouchRead(&tp);

    if (down & KEY_TOUCH) {
        if (ui_hit_theme(tp.px, tp.py)) {
            ui_set_theme(ui_theme_index_next());
            s_app.active_slider = -1;
            s_touch_consumed = true;
            return;
        }
        s_touch_consumed = false;
        s_app.active_slider = ui_hit_slider(tp.px, tp.py);
    }
    if (s_touch_consumed) return;  // a tap on a button, not the keyboard

    if (s_app.active_slider >= 0) {
        // A drag stays on the slider it started on.
        set_param(s_app.active_slider, ui_slider_value(tp.py));
        return;
    }

    // Keyboard: sliding across keys re-triggers, like a ribbon.
    int key = ui_hit_key(tp.px, tp.py);
    if (key < 0) {
        source_release(SOURCE_TOUCH);
    } else if (key != s_held[SOURCE_TOUCH].key) {
        source_press(SOURCE_TOUCH, key);
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
}

static void update_lit_keys(void) {
    memset(s_app.key_lit, 0, sizeof(s_app.key_lit));
    for (int s = 0; s < NUM_SOURCES; s++)
        if (s_held[s].key >= 0) s_app.key_lit[s_held[s].key] = true;
}

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

    engine_init();
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
        if (down & KEY_START) break;

#ifdef VM_DEMO
        // Debug attract mode (make DEMO=1): silent notes, rotating themes.
        if (s_app.frame == 1) engine_set_param(PARAM_VOLUME, 0.0f);
        if (s_app.frame % 240 == 239) ui_set_theme(ui_theme_index_next());
        if (s_app.frame % 12 == 0) source_press(0, (int)((s_app.frame / 12 * 5) % MUSIC_KEYS));
        if (s_app.frame % 12 == 8) source_release(0);
#endif
        handle_buttons(down, held, up, repeat);
        handle_touch(down, held, up);
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

    release_all();
    audio_out_exit();
    engine_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
