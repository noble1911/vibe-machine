// tables.h -- wavetables Vibe Machine builds for itself at start-up: rich
// sounds that cost one wavetable oscillator to play (AMY's w19 reads a
// table of 256-sample cycles and its `d` glides from cycle to cycle).
// Pure maths: tests/ checks them on the build machine.
#pragma once

#include <stdint.h>

#define TABLE_CYCLE 256
#define ORGAN_CYCLES 8   // drawbar settings, mellow flute -> full organ
#define VOX_CYCLES 5     // vowels U O A E I
#define ORGAN_PRESET 240 // AMY memory presets they're loaded into
#define VOX_PRESET 241

// ORGAN_CYCLES x TABLE_CYCLE samples: each cycle one drawbar registration.
void tables_organ(int16_t *out);
// VOX_CYCLES x TABLE_CYCLE samples: each cycle a sung vowel (its formants
// drawn onto the harmonics of a G3), U O A E I.
void tables_vox(int16_t *out);
