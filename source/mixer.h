// mixer.h -- the MIX page's channel strips: three parts and the drums, each
// with its own level, pan, mute and effects (DRIVE, CRUSH, TONE).
//
// Each strip is an AMY bus: part k plays on bus k, the drums on bus
// MIX_DRUMS. AMY runs the strip's TONE (its bus EQ); then mixer_hook()
// (AMY's per-bus post-process hook) runs DRIVE and CRUSH, applies the level
// and pan, and adds the strip into the master bus (the SOUND page's tone
// and volume), and -- as much as its ECHO and VERB sends say -- into an echo
// bus and a reverb bus. Those run AMY's echo / reverb once each, for every
// strip together; as AMY's effects add their wet sound on top of what goes
// in, the hook then takes the sends back out, leaving only the effect.
// So echo and reverb still cost one each, however many strips there are.
//
// No 3DS code here: tests/ builds this file against AMY on the host.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "engine.h"

#define MIX_STRIPS (PARTS + 1)          // parts 0..PARTS-1, then the drums
#define MIX_DRUMS PARTS                 // strip (and bus) of the drums
#define MIX_MASTER_BUS MIX_STRIPS       // where every strip ends up (dry)
#define MIX_ECHO_BUS (MIX_STRIPS + 1)   // the echo: fed by the strips' ECHO sends
#define MIX_VERB_BUS (MIX_STRIPS + 2)   // the reverb: fed by their VERB sends
#define MIX_BUSES (MIX_STRIPS + 3)
#define MIX_LEVEL_UNITY 0.75f           // fader position for 0 dB
// Synths feed their bus this much quieter (the hook makes it back up), so a
// part sits well inside the +-1.0 that DRIVE's clipper and CRUSH's steps are
// scaled to.
#define MIX_HEADROOM 0.2f

// (ECHO and VERB came later: they're last, so older saves read straight in.)
typedef enum { MIX_DRIVE, MIX_CRUSH, MIX_TONE, MIX_PAN, MIX_ECHO, MIX_VERB, MIX_KNOBS } MixKnob;

typedef struct MixStrip {
    float level;            // fader, 0..1 (MIX_LEVEL_UNITY = unchanged)
    float knob[MIX_KNOBS];  // 0..1; TONE and PAN are centred at 0.5
} MixStrip;

static inline void mixer_strip_default(MixStrip *s) {
    s->level = MIX_LEVEL_UNITY;
    s->knob[MIX_DRIVE] = 0.0f;
    s->knob[MIX_CRUSH] = 0.0f;
    s->knob[MIX_TONE] = 0.5f;
    s->knob[MIX_PAN] = 0.5f;
    s->knob[MIX_ECHO] = 1.0f;  // full send: the SOUND page's ECHO / VERB set how much
    s->knob[MIX_VERB] = 1.0f;
}
// What a sound's own level (AMY's iV) becomes on its way into a strip.
static inline float mixer_synth_level(float level) { return level * MIX_HEADROOM; }
static inline int mixer_strip_synth(int strip) { return strip == MIX_DRUMS ? SYNTH_DRUMS : part_synth(strip); }
const char *mixer_knob_name(MixKnob k);

// The AMY event (start from amy_default_event()) that sets a strip's TONE on
// its bus. Call from the engine, under its lock.
struct amy_event;
void mixer_fill_event(int strip, const MixStrip *s, struct amy_event *e);

// Level, pan, DRIVE, CRUSH and whether the strip can be heard (mute / solo).
// Takes effect on the next block.
void mixer_set_gain(int strip, const MixStrip *s, bool audible);

// AMY's bus post-process hook (amy_config.amy_external_bus_postprocess_hook).
void mixer_hook(uint16_t bus, int32_t *buf, uint16_t len);

// Loudest sample (0..1, after level and pan) a strip has sent since the last
// call. For the meters.
float mixer_peak(int strip);

// The master bus's volume (AMY's, as the SOUND page's VOL sets it), so that
// mixer_strip_block() comes out at the level you hear.
void mixer_set_master_volume(float volume);
// Audio thread, right after rendering a block: what one strip sent to the
// master during it, mono, full scale +-1.0 (as heard, before the master's
// tone, echo, reverb and the FX). AMY_BLOCK_SIZE frames. For the tape.
const float *mixer_strip_block(int strip);
