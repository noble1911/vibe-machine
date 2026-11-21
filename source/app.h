// app.h -- state shared between input handling (main.c) and drawing (ui.c).
#pragma once

#include <stdbool.h>

#include "engine.h"
#include "fx.h"
#include "drawwave.h"
#include "drums.h"
#include "mixer.h"
#include "synthedit.h"
#include "sampler.h"
#include "music.h"
#include "seq.h"
#include "songs.h"
#include "sounds.h"
#include "tape.h"

// PAGE_COUNT pages have tabs; SONGS opens from the SOUND page.
typedef enum { PAGE_SOUND, PAGE_SEQ, PAGE_SYNTH, PAGE_MIX, PAGE_FX, PAGE_TAPE, PAGE_COUNT, PAGE_SONGS = PAGE_COUNT } Page;

// What a touch on the MIX page grabbed (MIX_GRAB_KNOB + knob for a knob).
typedef enum { MIX_GRAB_NONE = -1, MIX_GRAB_FADER, MIX_GRAB_KNOB } MixGrab;

// Each instrument part keeps its own sound; AppState.sound / patch / macro
// below are a working copy of the current part's.
typedef struct {
    int sound, patch;
    float macro[SOUND_MACROS];
    PartEdit pe;    // the EDIT layer, and MORPH's snapshots
    DrawWave wave;  // the DRAW sound's waveform
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
    int key_root, key_scale;    // the song's key (music.h)
    SeqArp arp;                 // the arpeggiator's settings
    PlayMode mode;              // live mode from the shoulder buttons
    float params[PARAM_COUNT];  // slider positions, 0..1
    bool key_lit[MUSIC_KEYS];   // keys currently sounding (any source)
    int active_slider;          // slider being dragged, or -1
    bool audio_ok;              // false if ndsp could not start
    unsigned frame;             // frame counter, drives animation
    Page page;                  // bottom-screen page
    SeqPattern pattern;         // snapshot of the sequencer, taken each frame
    SeqStatus seq;
    bool edit_view;             // SYNTH: the pad shows the EDIT layer
    int edit_page;              // EditPage
    int edit_grab;              // what's being dragged in it (page-specific), or -1
    bool kit_view;              // SYNTH: the pad shows the drum KIT
    int kit_lane;               // the lane picked in it
    int kit_grab;               // KIT: 0, or 1 TUNE / 2 LEVEL being dragged
    DrumKit kit;                // the drum kit
    uint8_t drum_flash[8];      // KIT: frames each lane stays lit after a hit
    float bend;                 // pitch bend now (-1..1), from SELECT + circle pad
    int bar_page;               // the bar of the pattern the grid / roll / FX strip show (0..3)
    int bar_hold;               // frames BAR has been held (hold: change the length), or -1
    bool roll_view;             // SEQ page shows the piano roll instead of the grid
    bool arr_view;              // ...or the SONG sheet (the arrangement), which wins
    SeqArrangement arr;         // snapshot of the arrangement, taken each frame
    int arr_sel;                // SONG sheet: the picked row, or -1
    float clear_hold_t;         // 0..1 progress of holding ✕ (undo)
    int song_btn_hold;          // frames SONG has been held (hold: open the sheet), or -1
    bool roll_chromatic;        // roll rows: every semitone, or only the scale
    int roll_scroll;            // roll row shown at the bottom
    int roll_active;            // roll note being dragged, or -1
    uint8_t live_pitch[16];     // bit p = MIDI note p is being played live (shown on the roll)
    int slot_hold;              // slot sticker being held (to copy into), or -1
    float slot_hold_t;          // 0..1 progress of that hold

    // MIX page
    MixStrip mix[MIX_STRIPS];   // parts 0..PARTS-1, then the drums
    uint8_t mix_solo;           // bit n = strip n soloed (mutes live in seq.muted)
    float mix_meter[MIX_STRIPS];  // 0..1, with a fall-back, for the meters
    int mix_strip;              // strip being dragged, or -1
    int mix_grab;               // MixGrab: its fader, or MIX_GRAB_KNOB + knob
    // Story build (make STORY=1): the caption on the top screen, and the
    // pretend stylus on the bottom one.
    const char *caption;
    int caption_n;              // step number shown with it (0: none)
    bool cursor_on, cursor_down;
    float cursor_x, cursor_y;
    int mixdown;                // 0, 1 recording the song, 2 letting the echoes ring out
    float mixdown_secs;         // length written so far

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
    SampleEdit sample_edit;     // the MIC sound's START / END / CHOP
    int smp_grab;               // MIC editor: 0, 1 START / 2 END being dragged, 3 + m a slider,
                                // SMP_GRAB_CUT + k CHOP's cut k
#define SMP_GRAB_CUT 10
    int smp_flash, smp_flash_frames;  // a slice (or -1: the region) just auditioned
    bool lib_samples;           // the SONGS page is showing the sample library (or, with lib_presets, sounds)
    bool lib_presets;           // ... the saved sounds (PRESETS)

    // TAPE page
    TapeStatus tape;            // read back from the tape each frame
    bool tape_armed;            // TAPE's record is armed (it records while the tape plays)
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
    int notice_age;             // frames since it appeared (for the pop-up)

    bool pad_touch;             // stylus is on the SYNTH or FX pad
    float pad_x, pad_y;         // where (0..1, y up)
} AppState;
