// app.h -- state shared between input handling (main.c) and drawing (ui.c).
#pragma once

#include <stdbool.h>

#include "engine.h"
#include "music.h"
#include "seq.h"

typedef enum { PAGE_SOUND, PAGE_SEQ, PAGE_COUNT } Page;

typedef struct {
    int patch;                  // AMY patch number, PATCH_FIRST..PATCH_LAST
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
} AppState;
