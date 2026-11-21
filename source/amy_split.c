// amy_split.c -- see amy_split.h.

#include "amy_split.h"

#include "amy.h"

static int voice_of(uint16_t osc) {
    if (!osc_to_voice || AMY_IS_UNSET(osc_to_voice[osc])) return -1;
    return osc_to_voice[osc];
}

static int audible(uint16_t o) { return synth[o] != NULL && synth[o]->status == SYNTH_AUDIBLE; }

uint16_t amy_split_point_n(float core1_share, int *core0_oscs, int *core1_oscs) {
    int total = 0;
    for (uint16_t o = 0; o < AMY_OSCS; o++) total += audible(o);
    int seen = 0;
    uint16_t split = AMY_OSCS;
    if (total > 0) {
        const int core0 = (int)(total * (1.0f - core1_share) + 0.5f);
        for (uint16_t o = 0; o < AMY_OSCS; o++) {
            if (seen >= core0) {
                // Move on to the start of the next voice (or to a free osc).
                while (o < AMY_OSCS && o > 0 && voice_of(o) >= 0 && voice_of(o) == voice_of(o - 1)) seen += audible(o++);
                split = o;
                break;
            }
            seen += audible(o);
        }
    }
    if (core0_oscs) *core0_oscs = seen;
    if (core1_oscs) *core1_oscs = total - seen;
    return split;
}

uint16_t amy_split_point(float core1_share) { return amy_split_point_n(core1_share, NULL, NULL); }
