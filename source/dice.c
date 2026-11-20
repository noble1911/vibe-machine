// dice.c -- see dice.h.

#include "dice.h"

#include <string.h>

#include "rng.h"

// Lanes match the drum keys: kick, snare, hat, open hat, clap, rim, tom, crash.
enum { KICK, SNARE, HAT, OPEN, CLAP, RIM, TOM, CRASH };

// Chance (0..100) of a hit per step, per lane. Index = step % 16.
static const uint8_t kChance[SEQ_LANES][SEQ_STEPS] = {
    [KICK] = {95, 5, 15, 10, 40, 5, 30, 10, 85, 5, 30, 10, 35, 5, 20, 15},
    [SNARE] = {0, 0, 5, 0, 92, 5, 0, 15, 0, 10, 5, 0, 92, 0, 10, 25},
    [HAT] = {30, 20, 85, 25, 30, 20, 85, 25, 30, 20, 85, 25, 30, 20, 85, 40},
    [OPEN] = {0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 30, 0},
    [CLAP] = {0, 0, 0, 0, 25, 0, 0, 0, 0, 0, 0, 0, 25, 0, 0, 10},
    [RIM] = {0, 10, 0, 15, 0, 10, 0, 20, 0, 10, 0, 15, 0, 10, 0, 5},
    [TOM] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 20, 25},
    [CRASH] = {12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

// Keys of the C major pentatonic within the 8-key scale: C D E G A C.
static const int kPenta[] = {0, 1, 2, 4, 5, 7};

void dice_groove(SeqPattern *out, uint32_t seed, int octave) {
    Rng rng = {seed | 1u};
    memset(out, 0, sizeof(*out));
    out->len = SEQ_STEPS;
    for (int s = 0; s < SEQ_STEPS; s++)
        for (int lane = 0; lane < SEQ_LANES; lane++)
            if (rng_int(&rng, 100) < kChance[lane][s]) out->drums[s] |= (uint8_t)(1u << lane);
    out->drums[0] |= 1u << KICK;  // always land the one
    // Accents: the downbeats mostly, plus the odd surprise.
    for (int s = 0; s < SEQ_STEPS; s++)
        if (rng_int(&rng, 100) < (s % 4 == 0 ? 60 : 8)) out->accents |= (uint16_t)(1u << s);

    // Synth: a short motif of 4 steps, repeated with variations, so it sounds
    // like a phrase rather than noise. Chords now and then.
    int motif[4];
    for (int i = 0; i < 4; i++) motif[i] = rng_int(&rng, 100) < 60 ? kPenta[rng_int(&rng, 6)] : -1;
    if (motif[0] < 0) motif[0] = 0;
    for (int s = 0; s < SEQ_STEPS; s++) {
        int key = motif[s % 4];
        if (s >= 8 && rng_int(&rng, 100) < 30) key = rng_int(&rng, 100) < 50 ? -1 : kPenta[rng_int(&rng, 6)];
        if (key < 0) continue;
        bool chord = rng_int(&rng, 100) < 15;
        int notes[MUSIC_MAX_NOTES];
        int n = music_key_notes(key, chord ? PLAY_CHORD : PLAY_NOTE, octave, notes);
        int len = rng_int(&rng, 100) < 25 ? 2 : 1;  // now and then a longer note
        for (int i = 0; i < n && out->count < SEQ_NOTES; i++)
            out->notes[out->count++] = (SeqRollNote){(uint8_t)s, (uint8_t)notes[i], (uint8_t)len, (uint8_t)(SEQ_VEL_SYNTH * 255), 0, 0};
    }
}
