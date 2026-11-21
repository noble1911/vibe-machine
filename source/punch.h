// punch.h -- PUNCH: sixteen punch-in effects on the master, played from a
// 4x4 grid on the FX page (hold a pad: on; let go: off) and sequenced per
// step. Everything engages and lets go on the next 1/16 of the beat, and
// every repeat length is a note value at the current tempo.
//
//   LOOP 1/4 .. 1/32, TRIPLET  beat repeats: what plays from the 1/16 you
//                              hit, repeated at that note length
//   SHUFFLE   the last bar's sixteen 16ths, replayed in a new order
//   REVERSE   the last beat, backwards
//   SCRATCH   the record pulled back and forth under your hand
//   OCT UP / OCT DOWN  the mix at 2x / 0.5x speed, kept in time
//   RISER     pitch and a high-pass climbing for a bar
//   LO-FI     wow, flutter, a dull old speaker and crackle
//   GATE      a rhythmic chop, locked to the bar
//   KILL LO / KILL HI  DJ filter kills
//   DUB       a throw into a tempo-synced echo that rings on after release
//
// When several are on, they go in a fixed order: one of the repeaters
// (shortest LOOP first, then TRIPLET, SCRATCH, REVERSE, SHUFFLE) -> pitch
// (OCT, RISER) -> LO-FI -> KILL LO -> KILL HI -> the RISER's filter -> GATE
// -> DUB. Every change crossfades over a few milliseconds.
//
// Threads: punch_set_live() comes from the UI; punch_clock() and
// punch_process() run on the audio thread, once per block, after the
// sequencer's clock (seq_clock()). No 3DS dependencies: tests/ runs it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PUNCH_LOOP4,
    PUNCH_LOOP8,
    PUNCH_LOOP16,
    PUNCH_LOOP32,
    PUNCH_TRIPLET,
    PUNCH_SHUFFLE,
    PUNCH_REVERSE,
    PUNCH_SCRATCH,
    PUNCH_OCT_UP,
    PUNCH_OCT_DOWN,
    PUNCH_RISER,
    PUNCH_LOFI,
    PUNCH_GATE,
    PUNCH_KILL_LOW,
    PUNCH_KILL_HIGH,
    PUNCH_DUB,
    PUNCH_COUNT
} PunchKind;

#define PUNCH_BIT(k) ((uint16_t)(1u << (k)))
#define PUNCH_ALL ((uint16_t)0xFFFFu)

// GATE's pattern over a bar of 16ths (bit s = step s open).
#define PUNCH_GATE_PATTERN 0xAD6Du  // x.xx .xx. x.xx .x.x

typedef struct {
    uint16_t active;   // engaged now (changes on 1/16 ticks only)
    uint16_t want;     // held or sequenced: what will be engaged after the next tick
    int step;          // the sequencer step of the last tick, or -1 (stopped: free-running)
    int step16;        // 0..15: the 16th of the bar the GATE is on
    float gate;        // GATE's opening now, 0..1 (1 when GATE is off)
    float riser;       // RISER's climb, 0..1
    float scratch;     // SCRATCH's hand: -1 (pulled back) .. 1 (forward)
    float echo;        // DUB's echo level, 0..1 (rings on after release)
    int slice;         // SHUFFLE: the 16th of the old bar playing now, or -1
    uint32_t repeats;  // counts the repeats of a LOOP / TRIPLET (for visuals)
} PunchState;

void punch_init(int sample_rate);
// UI thread: the pads held now (HOLD's latched ones included). A pad
// tapped and let go between two ticks still engages at the next one.
void punch_set_live(uint16_t mask);
// Audio thread, before each block: `tick` = a 1/16 step fired at the
// start of this block (its `step`, and `pattern`: the effects that step
// has sequenced). While `playing` is false the 1/16s run free at `bpm`,
// starting from the first pad you press.
void punch_clock(bool tick, int step, bool playing, uint16_t pattern, float bpm);
// In place, interleaved stereo int16. With nothing on (and the echo rung
// out) the audio passes through bit for bit.
void punch_process(int16_t *stereo, int frames);
PunchState punch_state(void);

const char *punch_name(PunchKind k);   // "LOOP 1/4"
const char *punch_label(PunchKind k);  // short, for the pads: "1/4"
const char *punch_about(PunchKind k);  // one line of what it does
