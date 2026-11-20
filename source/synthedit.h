// synthedit.h -- the SYNTH page's EDIT layer: a part's sound, shaped further.
//
// Every parameter is 0..1. A parameter only overrides the sound once it has
// been touched (its bit in `set`); until then the sound plays as designed,
// and the value shown is the sound's own default (sounds.c). MORPH blends
// between two stored snapshots; MUTATE nudges what's there.
// No 3DS or AMY dependencies: the wire messages are built in sounds.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ED_ATTACK, ED_DECAY, ED_SUSTAIN, ED_RELEASE,  // amp envelope
    ED_CUTOFF, ED_RESO, ED_FENV,                  // filter; FENV 0.5 = no envelope, below = down
    ED_LFO_RATE, ED_LFO_DEPTH,                    // LFO (target below)
    ED_GLIDE,                                     // portamento time (0 = none)
    ED_DETUNE,                                    // unison spread
    ED_BRIGHT,                                    // DX7: how hard the modulators hit; others: drive
    ED_PARAMS
} EditParam;

typedef enum { LFO_PITCH, LFO_FILTER, LFO_AMP, LFO_PAN, LFO_TARGETS } LfoTarget;

#define UNISON_MAX 3

typedef struct {
    float p[ED_PARAMS];
    uint16_t set;        // bit n: p[n] overrides the sound
    uint8_t lfo_target;  // LfoTarget
    uint8_t mono;        // one voice, legato (with GLIDE: slides)
    uint8_t unison;      // 1..UNISON_MAX copies a voice
} SynthEdit;

// A part's EDIT state: the live edit plus MORPH's two snapshots.
typedef struct {
    SynthEdit cur;
    SynthEdit snap[2];
    float snap_macro[2][2];  // the sound's two macros, with each snapshot
    uint8_t snaps;           // bit s: snapshot s (A, B) has been stored
    float morph;             // 0 = A .. 1 = B
} PartEdit;

const char *edit_param_name(EditParam p);
// A parameter in real units: ms for the envelope and GLIDE (0 = none),
// level for SUSTAIN and DEPTH, Hz for CUTOFF and RATE, Q for RESO, octaves
// for F.ENV (+-6), cents for DETUNE, a factor for BRIGHT (1 = as designed).
float edit_real(EditParam p, float v);
// The same, as a short label: "12ms", "1.2s", "2.3k", "+3oct", "50%".
void edit_label(EditParam p, float v, char *out, int len);
const char *lfo_target_name(int t);
// The other way: real units back to 0..1 (clamped).
float edit_norm(EditParam p, float real);

// Untouched: the sound's own values (from `defaults`), nothing set.
void edit_reset(SynthEdit *e, const float defaults[ED_PARAMS]);
void edit_set(SynthEdit *e, EditParam p, float v);  // sets it (and marks it set)
void edit_clamp(SynthEdit *e);                      // sanitise (after loading)

// The edit `t` of the way from a to b (continuous parameters blend; the
// switches come from whichever is nearer; set = set in either).
void edit_morph(const SynthEdit *a, const SynthEdit *b, float t, SynthEdit *out);

// Nudge: every continuous parameter moves by up to +-amount (and becomes
// set). `seed` makes it repeatable.
void edit_mutate(SynthEdit *e, float amount, uint32_t seed);
