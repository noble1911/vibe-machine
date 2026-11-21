// amy_split.h -- dividing AMY's oscillators between the 3DS's two cores.
//
// AMY can render two ranges of oscillators at once, one per core, into
// separate buffers (built with VM_AMY_DUALCORE: see tools/amy-dualcore.sh).
// The split has to fall between voices -- a voice's oscillators read each
// other while they render -- and should balance the work: core 1 takes a
// little more than half, because core 0 also runs the effects afterwards.
#pragma once

#include <stdint.h>

// The first oscillator core 1 renders (core 0 does 0..split-1, core 1
// split..AMY_OSCS-1). `core1_share` is the fraction of the sounding
// oscillators to give core 1.
uint16_t amy_split_point(float core1_share);
// An oscillator's cost to render, in plain oscillators (0: not sounding).
// Shares and counts are in these units, so a DX7 voice (one ALGO osc, six
// operators) or a unison cluster weighs what it costs.
int amy_split_weight(uint16_t osc);
// The same, also counting the sounding oscillators each core gets (for
// balancing the share by how long each core takes).
uint16_t amy_split_point_n(float core1_share, int *core0_oscs, int *core1_oscs);
