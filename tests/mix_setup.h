// mix_setup.h -- the probes' version of engine.c's bus setup (mixer.h):
// the dry master, the echo bus and the reverb bus, all at volume 1.
#pragma once

#include "amy.h"
#include "mixer.h"

static inline void mix_setup(float echo, float verb) {
    for (int bus = MIX_MASTER_BUS; bus <= MIX_VERB_BUS; bus++) {
        amy_event v = amy_default_event();
        v.bus = (uint16_t)bus;
        v.volume = 1.0f;
        amy_add_event(&v);
    }
    amy_event e = amy_default_event();
    e.bus = MIX_ECHO_BUS;
    e.echo_level = echo;
    e.echo_delay_ms = 300.0f;
    e.echo_max_delay_ms = 750.0f;
    e.echo_feedback = 0.4f;
    amy_add_event(&e);
    amy_event r = amy_default_event();
    r.bus = MIX_VERB_BUS;
    r.reverb_level = verb;
    amy_add_event(&r);
}

static inline void mix_echo(float level) {
    amy_event e = amy_default_event();
    e.bus = MIX_ECHO_BUS;
    e.echo_level = level;
    amy_add_event(&e);
}

static inline void mix_verb(float level) {
    amy_event e = amy_default_event();
    e.bus = MIX_VERB_BUS;
    e.reverb_level = level;
    amy_add_event(&e);
}
