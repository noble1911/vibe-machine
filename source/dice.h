// dice.h -- musically-weighted random ideas. Pure logic (tests/ checks it).
#pragma once

#include <stdint.h>

#include "seq.h"

// A fresh groove: kicks favour the downbeats, snares beats 2 and 4, hats
// fill the gaps, and the synth line wanders a pentatonic scale with rests.
void dice_groove(SeqPattern *out, uint32_t seed, int octave);
