// seq.h -- the pattern sequencer: 16 steps, 8 pattern slots, song mode,
// swing, accents, drums on a grid and melodies as a piano roll.
//
// The clock runs on the audio thread: audio_out.c calls seq_advance() before
// rendering every block, so steps follow the sample clock rather than the
// 60 Hz screen. Everything else is called from the UI thread; a lock keeps
// the two sides consistent. Notes leave through a callback (the engine on
// the 3DS, a recorder in tests/), so this file also builds on a PC.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "music.h"

#define SEQ_STEPS 16          // steps in a bar (and on screen at once)
#define SEQ_BARS_MAX 4        // a pattern is 1, 2 or 4 bars long
#define SEQ_STEPS_MAX (SEQ_STEPS * SEQ_BARS_MAX)
#define SEQ_LANES MUSIC_KEYS  // one drum lane per key (kick, snare, hat...)
#define SEQ_SLOTS 8           // patterns A..H
#define SEQ_NOTES 128         // piano-roll notes per pattern
#define SEQ_BPM_MIN 40
#define SEQ_BPM_MAX 240
#define SEQ_SWING_MAX 0.5f    // even steps up to 50% longer, odd ones shorter
#define SEQ_GATE 0.55f        // a note sounds this far into its last step

// Velocities: accented steps hit harder.
#define SEQ_VEL_DRUM 0.72f
#define SEQ_VEL_DRUM_ACCENT 1.0f
#define SEQ_VEL_SYNTH 0.62f
#define SEQ_VEL_SYNTH_ACCENT 0.9f

// What a key press plays (a note, or a chord of up to 3): used for painting
// the grid's synth row and for live recording.
typedef struct {
    int8_t key;     // which key played it (for visuals), -1 if none
    uint8_t count;  // 0 = rest
    uint8_t notes[MUSIC_MAX_NOTES];
} SeqNote;

// One note on the piano roll.
typedef struct {
    uint8_t step;   // 0..len-1, where it starts
    uint8_t pitch;  // MIDI note
    uint8_t len;    // 1..SEQ_STEPS_MAX steps (may run past the end of the pattern)
    uint8_t vel;    // 1..255 (velocity = vel / 255)
    uint8_t part;   // which instrument part plays it (0..PARTS-1)
    uint8_t flags;  // SEQ_NOTE_SLIDE...
} SeqRollNote;

// A sliding note holds on into the next one (legato): on a MONO sound with
// GLIDE the pitch slides across instead of the note restriking (acid).
#define SEQ_NOTE_SLIDE 1
// An off-beat 1/32: the note starts half a step late (the arpeggiator at
// 1/32 records the notes between the steps like this). A one-step one
// sounds for a 32nd, and cuts the part's own short notes from that step.
#define SEQ_NOTE_HALF 2

typedef struct {
    uint8_t len;                   // steps: 16, 32 or 64 (0 counts as 16)
    uint8_t drums[SEQ_STEPS_MAX];  // bit n = drum lane n hits on this step
    uint64_t accents;              // bit s = step s is accented
    uint8_t count;                 // notes in use
    SeqRollNote notes[SEQ_NOTES];
    // FX automation: the FX page's pad, recorded per step. fx[s] is 0 for
    // nothing, else FxKind + 1; fx_x / fx_y its position (0..255).
    uint8_t fx[SEQ_STEPS_MAX], fx_x[SEQ_STEPS_MAX], fx_y[SEQ_STEPS_MAX];
    // PUNCH-ins sequenced per step: bit k = PunchKind k is on for that step.
    uint16_t punch[SEQ_STEPS_MAX];
} SeqPattern;

static inline int seq_len(const SeqPattern *p) { return p->len ? p->len : SEQ_STEPS; }
static inline int seq_bars(const SeqPattern *p) { return seq_len(p) / SEQ_STEPS; }

// The song's arrangement (SONG mode): rows played in order, each a slot
// repeated `reps` times with some parts dropped out. Empty: SONG chains
// the slots that have something in them, as before.
#define SEQ_ARR_ROWS 32
#define SEQ_ARR_REPS_MAX 8
typedef struct {
    uint8_t slot;   // 0..SEQ_SLOTS-1
    uint8_t reps;   // 1..SEQ_ARR_REPS_MAX bars
    uint8_t mutes;  // bit n = part n silent in this row; bit PARTS = the drums
} SeqArrRow;
typedef struct {
    uint8_t count;
    bool loop;      // at the end: start again (true) or stop (false)
    SeqArrRow rows[SEQ_ARR_ROWS];
} SeqArrangement;

typedef struct {
    bool playing;
    bool recording;
    int bpm;
    int step;        // step most recently fired (valid while playing), 0..len-1
    uint32_t fired;  // increments every time a step fires
    int slot;        // pattern playing / being edited (0..7 = A..H)
    int queued;      // slot that takes over at the next bar, or -1
    bool song;       // chain the non-empty slots in order
    float swing;     // 0..SEQ_SWING_MAX
    uint8_t used;    // bit n = slot n has something in it
    uint8_t muted;   // bit n = part n is muted; bit PARTS = the drums
    int arr_row;     // arrangement row playing (SONG mode with an arrangement), else -1
    int arr_rep;     // which repeat of it (0-based)
    uint32_t ends;   // bumps each time an arrangement plays to its end and stops
    uint8_t slot_bars[SEQ_SLOTS];  // each slot's length in bars
} SeqStatus;

// The arpeggiator: plays the notes you hold one at a time, on the
// sequencer's clock (locked to the beat while it plays, free-running at the
// tempo while it doesn't). While recording, what it plays goes into the
// pattern.
typedef enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_MODES } ArpMode;
typedef enum { ARP_8TH, ARP_16TH, ARP_32ND, ARP_RATES } ArpRate;
typedef struct {
    bool on;
    uint8_t mode;     // ArpMode
    uint8_t rate;     // ArpRate
    uint8_t octaves;  // 1 or 2
} SeqArp;

// velocity 0 means note-off. `synth` is SYNTH_DRUMS or part_synth(part).
typedef void (*SeqNoteFn)(int synth, int note, float velocity);

void seq_init(SeqNoteFn out);

// --- audio thread --------------------------------------------------------
// Advance the clock by one block of `frames` at `rate` Hz and send any
// notes that fall due.
void seq_advance(int frames, int rate);

// What the last seq_advance() did, for the PUNCH effects (audio thread
// only, right after it): whether a step fired (at the start of the block
// about to render), which, and the punch-ins it has sequenced.
typedef struct {
    bool playing, fired;
    int step;
    uint16_t punch;
    int bpm;
} SeqClock;
SeqClock seq_clock(void);

// --- UI thread -----------------------------------------------------------
void seq_toggle_play(void);
void seq_set_recording(bool on);
void seq_set_bpm(int bpm);
void seq_set_swing(float swing);
void seq_set_song(bool on);
void seq_set_arrangement(const SeqArrangement *a);
void seq_get_arrangement(SeqArrangement *out);
void seq_set_part_mute(int part, bool muted);  // part PARTS = the drums
void seq_clear(void);                       // the current slot
void seq_reset(void);  // a new song: empty slots, starter groove in A, no swing / song
void seq_set_pattern(const SeqPattern *p);  // the current slot
// The current slot's length: 1, 2 or 4 bars. Growing it repeats what's
// there into the new bars (ready to vary); shrinking hides the bars past
// the end (they come back if it grows again).
void seq_set_bars(int bars);

void seq_set_arp(const SeqArp *a);
// The notes held for the arpeggiator, for `part` (replaces the last set;
// n = 0 lets go).
void seq_arp_hold(int part, const int *pitches, int n);

// Switch pattern: immediately when stopped, at the next bar while playing.
void seq_select_slot(int slot);
// Copy the current pattern into `slot` and switch to it.
void seq_copy_to_slot(int slot);

void seq_set_drum(int step, int lane, bool on);
void seq_set_accent(int step, bool on);
// The grid's synth row for one part: replace whatever that part starts on
// `step` with `note` (one step long), or clear it (note NULL or count 0).
void seq_set_synth(int part, int step, const SeqNote *note);

// Piano roll (current slot). Indices are positions in SeqPattern.notes and
// stay valid until a note is removed.
int seq_roll_add(int part, int step, int pitch, int len, int vel);  // index, or -1 if full
void seq_roll_set(int index, int step, int pitch, int len);
void seq_roll_remove(int index);
void seq_roll_set_vel(int index, int vel);  // 1..255
void seq_roll_set_flags(int index, int flags);
// FX automation on `step` of the current slot: kind1 = FxKind + 1, or 0 to
// erase; x, y 0..1.
void seq_set_fx(int step, int kind1, float x, float y);
// PUNCH-ins on `step` of the current slot (bit k = PunchKind k).
void seq_set_punch(int step, uint16_t mask);
// The part's note covering (step, pitch), i.e. starting at or before step
// and still sounding there; -1 if none.
int seq_roll_find(int part, int step, int pitch);
// Every note of the parts in `parts` (bit n = part n), in every slot, moves
// to the pitch `map` gives it (notes that land on the same spot become one).
void seq_map_pitches(uint8_t parts, int (*map)(int pitch, void *ctx), void *ctx);
// Replace one part's notes in the current slot with those in `p` (whose
// notes are taken as that part's); drums and accents come from `p` too.
void seq_set_part_pattern(int part, const SeqPattern *p);

// Live recording (only acts while playing and recording): the hit lands on
// the step nearest to what the player heard when they pressed. Synth notes
// are as long as you hold them: call seq_record_release() with the same
// note when the key comes up.
// The clock runs ahead of the speaker by the audio queued for output: the
// audio thread says by how much, in frames, as it changes. And a press
// reaches the sequencer a little after it happened (input is read once a
// frame): the UI says how long that is on average, in frames.
void seq_set_latency(int frames);
void seq_set_input_delay(int frames);
void seq_record_drum(int lane);
void seq_record_synth(int part, const SeqNote *note);
void seq_record_release(int part, const SeqNote *note);

SeqStatus seq_status(void);
void seq_snapshot(SeqPattern *out);  // the current slot

// Whole-bank access for saving and loading.
void seq_get_slot(int slot, SeqPattern *out);
void seq_put_slot(int slot, const SeqPattern *p);

// Notes of `part` (or any part, if -1) in `p` that start on `step`.
int seq_notes_at(const SeqPattern *p, int part, int step, int *pitches, int max);
