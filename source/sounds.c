// sounds.c -- see sounds.h.
//
// AMY patch strings in brief (see third_party/amy/docs/api.md):
//   vN  osc N (voice-relative)      wN  wave: 0 sine 1 pulse 2 saw 4 tri
//   5 noise 6 Karplus-Strong 19 wavetable 20 silent
//   aC,N,V,E0   amp coefs           fC,N,V,E0,E1,M0,BEND  freq coefs
//   FC,N,V,E0   filter cutoff       G  filter type (1 LP, 2 BP, 4 LP24)
//   R  resonance                    AT,V,T,V,...  envelope 0 (last = release)
//   L  modulation source osc        d  duty / pluck / morph coefs
//   b  KS feedback                  C  unison count,spacing,offset,blend
//   GF fold  GD drive  GM mix  GH crush bits,rate     p  preset
// Commands are separated by Z.

#include "sounds.h"

#include <math.h>
#include <stdio.h>

// Mascot shapes; must match MotifKind in motif.h (kept numeric here so this
// file stays free of UI headers).
enum { M_TRI = 0, M_TRI_OUTLINE, M_TRI_STRIPED, M_TRI_DOTTED, M_CIRCLE, M_RING, M_BAR,
       M_SQUIGGLE, M_ZIGZAG, M_BOLT, M_DOTS, M_CROSS, M_SPOT };

enum { S_JUNO, S_DX7, S_PLUCK, S_SQUELCH, S_BELL, S_BREEZE, S_SUPERSAW, S_FOLD, S_WAVY, S_MIC, S_COUNT };

static const Sound kSounds[S_COUNT] = {
    [S_JUNO] = {"JUNO", "JUNO", "128 vintage poly patches", M_TRI, SOUND_BANK, 0, 128, 6, 2.2f, 0, {0, 0}, {0, 0}},
    [S_DX7] = {"DX7", "DX7", "128 glassy FM patches", M_DOTS, SOUND_BANK, 128, 128, 6, 1.0f, 0, {0, 0}, {0, 0}},
    [S_PLUCK] = {"PLUCK", "PLUK", "plucked strings that ring", M_ZIGZAG, SOUND_CUSTOM, 0, 0, 6, 4.5f,
                 "v0w6b0.996d0.3A0,1,10000,1,900,0",
                 {"DECAY", "PICK"}, {0.7f, 0.35f}},
    [S_SQUELCH] = {"SQUELCH", "SQLC", "acid-wet pulse bass", M_SPOT, SOUND_CUSTOM, 0, 0, 4, 0.8f,
                   "v1w0f0.6,0a1,,0,0Zv0w1L1d0.5,,,,,0.3F250,0.5,,3.5R4G4A3,1,250,0.45,120,0",
                   {"CUTOFF", "RESO"}, {0.35f, 0.45f}},
    [S_BELL] = {"BELL", "BELL", "shimmery FM chimes", M_RING, SOUND_CUSTOM, 0, 0, 6, 6.0f,
                // Two-operator FM (AMY's ALGO, DX7 algorithm 1): osc 2 modulates
                // osc 1. The modulator's decaying envelope is the FM index.
                "v2w0I3.5a2,,0,1A0,1,1500,0,300,0Zv1w0I1a1,,0,1A0,1,3000,0,600,0Zv0w8o1O,,,,2,1",
                {"RATIO", "SHINE"}, {0.55f, 0.35f}},
    [S_BREEZE] = {"BREEZE", "WIND", "whistling filtered wind", M_SQUIGGLE, SOUND_CUSTOM, 0, 0, 4, 1.5f,
                  "v0w5G2F440,1R10a1.5A250,1,600,0.6,900,0",
                  {"WHISTLE", "SHIFT"}, {0.6f, 0.5f}},
    [S_SUPERSAW] = {"SUPERSAW", "SAW", "five saws, one big chord", M_TRI_STRIPED, SOUND_CUSTOM, 0, 0, 4, 1.6f,
                    "v0w2C5,0.012,-0.024,0.85F1800,0.3,,2R1.2G4A10,1,400,0.7,300,0",
                    {"SPREAD", "CUTOFF"}, {0.4f, 0.55f}},
    [S_FOLD] = {"FOLD", "FOLD", "wavefolded crunch lead", M_BOLT, SOUND_CUSTOM, 0, 0, 4, 0.7f,
                "v0w4GF1GD3GM1A5,1,350,0.6,250,0",
                {"FOLD", "CRUSH"}, {0.4f, 0.0f}},
    [S_WAVY] = {"WAVY", "WAVY", "morphing wavetables", M_TRI_DOTTED, SOUND_CUSTOM, 0, 0, 4, 1.6f,
                "v1w0f0.25,0a1,,0,0Zv0w19p11L1d0.5,,,,,0.45A20,1,500,0.7,400,0",
                {"MORPH", "TABLE"}, {0.5f, 0.0f}},
    // Plays AMY memory PCM preset 200 (SAMPLER_PRESET): the microphone take.
    [S_MIC] = {"MIC", "MIC", "your voice, on the keys", M_RING, SOUND_CUSTOM, 0, 0, 4, 2.0f,
               "v0w7p200F9000,0.2G1R0.8A2,1,30,1,250,0",
               {"START", "TONE"}, {0.0f, 0.85f}},
};

int sound_count(void) { return S_COUNT; }

const Sound *sound_get(int index) {
    if (index < 0) index = 0;
    if (index >= S_COUNT) index = S_COUNT - 1;
    return &kSounds[index];
}

static int s_sample_frames = 1;  // length of the MIC take, for START

void sound_set_sample_frames(int frames) { s_sample_frames = frames > 0 ? frames : 1; }

static float lin(float t, float lo, float hi) { return lo + (hi - lo) * t; }
static float expo(float t, float lo, float hi) { return lo * powf(hi / lo, t); }

bool sound_macro_wire(int index, int m, float t, char *out, size_t len) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    switch (index * SOUND_MACROS + m) {
        // PLUCK: string feedback, pluck position (near the end = brighter).
        case S_PLUCK * 2 + 0: snprintf(out, len, "i1v0b%.4f", lin(t, 0.95f, 0.9995f)); return true;
        case S_PLUCK * 2 + 1: snprintf(out, len, "i1v0d%.3f", lin(t, 0.05f, 0.5f)); return true;
        // SQUELCH: filter cutoff and resonance.
        case S_SQUELCH * 2 + 0: snprintf(out, len, "i1v0F%.1f", expo(t, 60.0f, 4000.0f)); return true;
        case S_SQUELCH * 2 + 1: snprintf(out, len, "i1v0R%.2f", expo(t, 0.7f, 12.0f)); return true;
        // BELL: modulator:carrier ratio (stepped through bell-ish ratios) and
        // modulation depth (the modulator's peak amplitude, "beta").
        case S_BELL * 2 + 0: {
            static const float ratios[] = {0.5f, 1.0f, 1.41f, 2.0f, 2.76f, 3.5f, 4.0f, 5.19f, 7.0f};
            snprintf(out, len, "i1v2I%.2f", ratios[(int)(t * 8.99f)]);
            return true;
        }
        case S_BELL * 2 + 1: snprintf(out, len, "i1v2a%.2f", lin(t, 0.0f, 4.0f)); return true;
        // BREEZE: band-pass resonance, filter offset from the played note.
        case S_BREEZE * 2 + 0: snprintf(out, len, "i1v0R%.2f", expo(t, 1.0f, 16.0f)); return true;
        case S_BREEZE * 2 + 1: snprintf(out, len, "i1v0F%.1f", expo(t, 110.0f, 1760.0f)); return true;
        // SUPERSAW: detune spread (kept centred) and cutoff.
        case S_SUPERSAW * 2 + 0: {
            float sp = lin(t, 0.0f, 0.03f);
            snprintf(out, len, "i1v0C5,%.4f,%.4f,0.85", sp, -2.0f * sp);
            return true;
        }
        case S_SUPERSAW * 2 + 1: snprintf(out, len, "i1v0F%.1f", expo(t, 200.0f, 9000.0f)); return true;
        // FOLD: wavefold drive, then the bitcrusher (off at the bottom).
        case S_FOLD * 2 + 0: snprintf(out, len, "i1v0GD%.2f", expo(t, 1.0f, 12.0f)); return true;
        case S_FOLD * 2 + 1:
            if (t < 0.05f)
                snprintf(out, len, "i1v0GH0");
            else
                snprintf(out, len, "i1v0GH%d,%d", 14 - (int)(t * 11.0f), 1 + (int)(t * 6.0f));
            return true;
        // WAVY: position within the table, which of the five tables.
        case S_WAVY * 2 + 0: snprintf(out, len, "i1v0d%.3f", t); return true;
        case S_WAVY * 2 + 1: snprintf(out, len, "i1v0p%d", 11 + (int)(t * 4.99f)); return true;
        // MIC: where in the take each note starts, and a low-pass tone.
        // AMY's PCM phase is an absolute position: start_frame / 2^23.
        case S_MIC * 2 + 0:
            snprintf(out, len, "i1v0P%.7f", lin(t, 0.0f, 0.8f) * (float)s_sample_frames / 8388608.0f);
            return true;
        case S_MIC * 2 + 1: snprintf(out, len, "i1v0F%.0f", expo(t, 300.0f, 12000.0f)); return true;
        default: return false;
    }
}

int sound_mic_index(void) { return S_MIC; }
