// app.h -- state shared between input handling (main.c) and drawing (ui.c).
#pragma once

#include <stdbool.h>

#include "engine.h"
#include "fx.h"
#include "sampler.h"
#include "music.h"
#include "seq.h"
#include "songs.h"
#include "sounds.h"
#include "tape.h"

// PAGE_COUNT pages have tabs; SONGS opens from the SOUND page.
typedef enum { PAGE_SOUND, PAGE_SEQ, PAGE_SYNTH, PAGE_FX, PAGE_TAPE, PAGE_COUNT, PAGE_SONGS = PAGE_COUNT } Page;

// Each instrument part keeps its own sound; AppState.sound / patch / macro
// below are a working copy of the current part's.
typedef struct {
    int sound, patch;
    float macro[SOUND_MACROS];
} PartState;

typedef struct {
    int part;                   // part being played / edited (0..PARTS-1)
    int part_hold;              // SYNTH part sticker being held (to mute), or -1
    float part_hold_t;
    PartState parts[PARTS];
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
    bool roll_view;             // SEQ page shows the piano roll instead of the grid
    bool roll_chromatic;        // roll rows: every semitone, or only the scale
    int roll_scroll;            // roll row shown at the bottom
    int roll_active;            // roll note being dragged, or -1
    uint8_t live_pitch[16];     // bit p = MIDI note p is being played live (shown on the roll)
    int slot_hold;              // slot sticker being held (to copy into), or -1
    float slot_hold_t;          // 0..1 progress of that hold

    // FX page
    FxState fx;                 // what the audio thread is doing (read back each frame)
    int fx_sel;                 // FX shown on the pad
    bool fx_hold;               // HOLD: effects stay on after the stylus lifts
    bool fx_latched[FX_COUNT];  // effects left on by HOLD
    float fx_x[FX_COUNT], fx_y[FX_COUNT];
    // MIC sampler
    bool mic_ok;                // microphone service available
    bool mic_rec;               // recording right now
    float mic_level;            // live input level, 0..1
    float mic_secs;             // length of the take so far / of the loaded take
    float take_overview[SAMPLER_OVERVIEW];  // waveform of the loaded take

    // TAPE page
    TapeStatus tape;            // read back from the tape each frame
    bool tape_link;             // tape PLAY / REWIND also drive the sequencer
    int tape_hold_track;        // track sticker being held (to clear), or -1
    float tape_hold;            // 0..1 progress of that hold

    // Songs
    char song_name[SONG_NAME_MAX + 1];  // "" until saved or loaded
    bool song_dirty;            // changes since it was last saved / loaded
    SongInfo songs[SONGS_MAX];  // the SONGS page list
    int song_count, song_sel, song_scroll;
    int song_hold;              // SONGS button being held, or -1
    float song_hold_t;
    const char *busy;           // "SAVING..." etc. while a slow job runs, else NULL
    char confirm_name[SONG_NAME_MAX + 1];  // SAVE AS would replace this song: hold to confirm
    char notice[40];            // a short message ("SAVE FAILED"), shown while notice_frames > 0
    int notice_frames;

    bool pad_touch;             // stylus is on the SYNTH or FX pad
    float pad_x, pad_y;         // where (0..1, y up)
} AppState;
