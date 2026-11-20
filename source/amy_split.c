// amy_split.c -- see amy_split.h.

#include "amy_split.h"

#include "amy.h"

static int voice_of(uint16_t osc) {
    if (!osc_to_voice || AMY_IS_UNSET(osc_to_voice[osc])) return -1;
    return osc_to_voice[osc];
}

uint16_t amy_split_point(float core1_share) {
    int audible = 0;
    for (uint16_t o = 0; o < AMY_OSCS; o++) audible += synth[o] != NULL && synth[o]->status == SYNTH_AUDIBLE;
    if (audible == 0) return AMY_OSCS;
    const int core0 = (int)(audible * (1.0f - core1_share) + 0.5f);
    int seen = 0;
    for (uint16_t o = 0; o < AMY_OSCS; o++) {
        if (seen >= core0) {
            // Move on to the start of the next voice (or to a free osc).
            while (o < AMY_OSCS && o > 0 && voice_of(o) >= 0 && voice_of(o) == voice_of(o - 1)) o++;
            return o;
        }
        seen += synth[o] != NULL && synth[o]->status == SYNTH_AUDIBLE;
    }
    return AMY_OSCS;
}
