// app.h -- state shared between input handling (main.c) and drawing (ui.c).
#pragma once

#include <stdbool.h>

#include "engine.h"
#include "fx.h"
#include "music.h"
#include "seq.h"
#include "sounds.h"

typedef enum { PAGE_SOUND, PAGE_SEQ, PAGE_SYNTH, PAGE_FX, PAGE_COUNT } Page;

typedef struct {
    int sound;                  // index into sounds.c
    int patch;                  // AMY patch number (BANK sounds), PATCH_FIRST..PATCH_LAST
    float macro[SOUND_MACROS];  // CUSTOM sound macros, 0..1
    int octave;                 // MUSIC_OCTAVE_MIN..MUSIC_OCTAVE_MAX
    PlayMode mode;              // live mode from the shoulder buttons
    float params[PARAM_COUNT];  // slider positions, 0..1
    bool key_lit[MUSIC_KEYS];   // keys currently sounding (any source)
    int active_slider;          // slider being dragged, or -1
    bool audio_ok;              // false if ndsp could not start
    unsigned frame;             // frame counter, drives animation
    Page page;                  // bottom-screen page
    SeqPattern pattern;         // snapshot of the sequencer, taken each frame
    SeqStatus seq;

    // FX page
    FxState fx;                 // what the audio thread is doing (read back each frame)
    int fx_sel;                 // FX shown on the pad
    bool fx_hold;               // HOLD: effects stay on after the stylus lifts
    bool fx_latched[FX_COUNT];  // effects left on by HOLD
    float fx_x[FX_COUNT], fx_y[FX_COUNT];
    bool pad_touch;             // stylus is on the SYNTH or FX pad
    float pad_x, pad_y;         // where (0..1, y up)
} AppState;
