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

// AMY's built-in wavetables (WAVY) come after its built-in drum samples:
// the full TR-808 bank in the Gamma9001 build (the app), the small one
// otherwise.
#ifdef GAMMA9001
#define WT_BASE 19
#else
#define WT_BASE 11
#endif
#define STR_(x) #x
#define STR(x) STR_(x)

enum { S_JUNO, S_DX7, S_PLUCK, S_SQUELCH, S_BELL, S_BREEZE, S_SUPERSAW, S_FOLD, S_WAVY, S_MIC, S_DRAW,
       S_CHIP, S_BOOM, S_ORGAN, S_VOX, S_REESE, S_STRINGS, S_MALLET, S_EPIANO, S_ZAP, S_SAMPLE, S_COUNT };

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
                "v1w0f0.25,0a1,,0,0Zv0w19p" STR(WT_BASE) "L1d0.5,,,,,0.45A20,1,500,0.7,400,0",
                {"MORPH", "TABLE"}, {0.5f, 0.0f}},
    // Plays AMY memory PCM preset 200 (SAMPLER_PRESET): the take's START..END
    // region (or, chopped, a slice per key: see engine.c).
    [S_MIC] = {"MIC", "MIC", "your voice, on the keys", M_RING, SOUND_CUSTOM, 0, 0, 4, 2.0f,
               "v0w7p200F9000,0.2G1R0.8A2,1,30,1,250,0",
               {"DECAY", "TONE"}, {1.0f, 0.85f}},
    // One drawn cycle (DRAW_PRESET + part), played as a wavetable: in tune
    // on every key, and it can be redrawn while a note sounds.
    [S_DRAW] = {"DRAW", "DRAW", "a wave you draw yourself", M_CROSS, SOUND_CUSTOM, 0, 0, 4, 1.4f,
                "v0w19p220G1F7000,0.2R0.7A5,1,300,0.8,250,0",
                {"CUTOFF", "SOFT"}, {0.7f, 0.0f}},
    // --- page 2 ---
    // A handheld-game pulse: the duty in its classic steps, chip vibrato.
    [S_CHIP] = {"CHIP", "CHIP", "8-bit cartridge pulse", M_BAR, SOUND_CUSTOM, 0, 0, 4, 1.1f,
                "v1w0f6,0a1,,0,0Zv0w1d0.25L1f,,,,,0A2,1,120,0.75,80,0",
                {"DUTY", "VIBRATO"}, {0.5f, 0.0f}},
    // An 808 boom: a sine that drops from high (pitch envelope on eg1, B)
    // into the note, through a little folding drive.
    [S_BOOM] = {"BOOM", "BOOM", "808 bass that drops", M_CIRCLE, SOUND_CUSTOM, 0, 0, 4, 1.1f,
                "v0w0f,,,,3B0,1,90,0,0,0GF1GD1.5GM0.3A0,1,700,0.5,250,0",
                {"DROP", "DRIVE"}, {0.5f, 0.3f}},
    // Drawbars: a wavetable we build (tables.c), DRAWBARS glides through
    // eight registrations; the LFO is a rotating-speaker tremolo.
    [S_ORGAN] = {"ORGAN", "ORGN", "drawbars, spinning speaker", M_TRI_OUTLINE, SOUND_CUSTOM, 0, 0, 6, 0.45f,
                 "v1w0f1.5,0a1,,0,0Zv0w19p240L1a,,,,,0.25d0.3A8,1,40,1,60,0",
                 {"DRAWBARS", "WOBBLE"}, {0.3f, 0.3f}},
    // Five sung vowels as a wavetable (tables.c), with a touch of vibrato.
    [S_VOX] = {"VOX", "VOX", "a choir: aah, ooh, eeh", M_SPOT, SOUND_CUSTOM, 0, 0, 4, 1.8f,
               "v1w0f5.5,0a1,,0,0Zv0w19p241L1f,,,,,0.006d0.5A150,1,300,0.9,400,0",
               {"VOWEL", "VIBRATO"}, {0.5f, 0.3f}},
    // Two detuned saws beating against each other, low-passed: a growl.
    [S_REESE] = {"REESE", "RESE", "two saws, growling", M_ZIGZAG, SOUND_CUSTOM, 0, 0, 3, 1.05f,
                 "v0w2C2,0.012,-0.006,1F700,0.3,,1G4R1.8A5,1,300,0.9,200,0",
                 {"DETUNE", "CUTOFF"}, {0.4f, 0.45f}},
    // Three saws, a slow swell and a soft filter: an ensemble pad.
    [S_STRINGS] = {"STRINGS", "STRG", "slow, lush strings", M_SQUIGGLE, SOUND_CUSTOM, 0, 0, 4, 1.5f,
                   "v0w2C3,0.008,-0.008,0.9F2400,0.3G1R0.7A450,1,600,0.85,900,0",
                   {"SPREAD", "ATTACK"}, {0.5f, 0.45f}},
    // A short plucked-string model: marimba and kalimba woods to metal.
    [S_MALLET] = {"MALLET", "MALT", "marimba to kalimba", M_DOTS, SOUND_CUSTOM, 0, 0, 6, 7.0f,
                  "v0w6b0.985d0.1A0,1,3000,0,250,0",
                  {"TONE", "DECAY"}, {0.3f, 0.5f}},
    // Two-operator FM with a bright attack that mellows: a soft tine piano.
    [S_EPIANO] = {"E.PIANO", "EPNO", "soft electric-piano tines", M_RING, SOUND_CUSTOM, 0, 0, 6, 4.0f,
                  "v2w0I1a2.2,,0,1A0,1,600,0,250,0Zv1w0I1a1,,0,1A0,1,3500,0.25,500,0Zv0w8o1O,,,,2,1",
                  {"BARK", "TINE"}, {0.4f, 0.0f}},
    // A sine with a big fast pitch sweep: lasers and sci-fi toms.
    [S_ZAP] = {"ZAP", "ZAP", "lasers and sci-fi toms", M_BOLT, SOUND_CUSTOM, 0, 0, 4, 4.0f,
               "v0w0f,,,,5B0,1,140,0,0,0A0,1,220,0,60,0",
               {"SWEEP", "LENGTH"}, {0.6f, 0.3f}},
    // Any WAV from the library (or the MIC sample), its own per part:
    // memory preset PART_SAMPLE_PRESET + part.
    [S_SAMPLE] = {"SAMPLE", "SMPL", "your own sample, on keys", M_CROSS, SOUND_CUSTOM, 0, 0, 4, 2.7f,
                  "v0w7p250F9000,0.2G1R0.8A2,1,30,1,250,0",
                  {"DECAY", "TONE"}, {1.0f, 0.85f}},
};

int sound_count(void) { return S_COUNT; }

const Sound *sound_get(int index) {
    if (index < 0) index = 0;
    if (index >= S_COUNT) index = S_COUNT - 1;
    return &kSounds[index];
}



static float lin(float t, float lo, float hi) { return lo + (hi - lo) * t; }
static float expo(float t, float lo, float hi) { return lo * powf(hi / lo, t); }

bool sound_macro_wire(int index, int m, float t, int synth, char *out, size_t len) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    switch (index * SOUND_MACROS + m) {
        // PLUCK: string feedback, pluck position (near the end = brighter).
        case S_PLUCK * 2 + 0: snprintf(out, len, "i%dv0b%.4f", synth, lin(t, 0.95f, 0.9995f)); return true;
        case S_PLUCK * 2 + 1: snprintf(out, len, "i%dv0d%.3f", synth, lin(t, 0.05f, 0.5f)); return true;
        // SQUELCH: filter cutoff and resonance.
        case S_SQUELCH * 2 + 0: snprintf(out, len, "i%dv0F%.1f", synth, expo(t, 60.0f, 4000.0f)); return true;
        case S_SQUELCH * 2 + 1: snprintf(out, len, "i%dv0R%.2f", synth, expo(t, 0.7f, 12.0f)); return true;
        // BELL: modulator:carrier ratio (stepped through bell-ish ratios) and
        // modulation depth (the modulator's peak amplitude, "beta").
        case S_BELL * 2 + 0: {
            static const float ratios[] = {0.5f, 1.0f, 1.41f, 2.0f, 2.76f, 3.5f, 4.0f, 5.19f, 7.0f};
            snprintf(out, len, "i%dv2I%.2f", synth, ratios[(int)(t * 8.99f)]);
            return true;
        }
        case S_BELL * 2 + 1: snprintf(out, len, "i%dv2a%.2f", synth, lin(t, 0.0f, 4.0f)); return true;
        // BREEZE: band-pass resonance, filter offset from the played note.
        case S_BREEZE * 2 + 0: snprintf(out, len, "i%dv0R%.2f", synth, expo(t, 1.0f, 16.0f)); return true;
        case S_BREEZE * 2 + 1: snprintf(out, len, "i%dv0F%.1f", synth, expo(t, 110.0f, 1760.0f)); return true;
        // SUPERSAW: detune spread (kept centred) and cutoff.
        case S_SUPERSAW * 2 + 0: {
            float sp = lin(t, 0.0f, 0.03f);
            snprintf(out, len, "i%dv0C5,%.4f,%.4f,0.85", synth, sp, -2.0f * sp);
            return true;
        }
        case S_SUPERSAW * 2 + 1: snprintf(out, len, "i%dv0F%.1f", synth, expo(t, 200.0f, 9000.0f)); return true;
        // FOLD: wavefold drive, then the bitcrusher (off at the bottom).
        case S_FOLD * 2 + 0: snprintf(out, len, "i%dv0GD%.2f", synth, expo(t, 1.0f, 12.0f)); return true;
        case S_FOLD * 2 + 1:
            if (t < 0.05f)
                snprintf(out, len, "i%dv0GH0", synth);
            else
                snprintf(out, len, "i%dv0GH%d,%d", synth, 14 - (int)(t * 11.0f), 1 + (int)(t * 6.0f));
            return true;
        // WAVY: position within the table, which of the five tables.
        case S_WAVY * 2 + 0: snprintf(out, len, "i%dv0d%.3f", synth, t); return true;
        case S_WAVY * 2 + 1: snprintf(out, len, "i%dv0p%d", synth, WT_BASE + (int)(t * 4.99f)); return true;
        // MIC: how long each note lasts while held (short = tight chops; at
        // the top it plays to the end), and a low-pass tone.
        case S_MIC * 2 + 0:
            if (t > 0.97f)
                snprintf(out, len, "i%dv0A2,1,30,1,250,0", synth);
            else
                snprintf(out, len, "i%dv0A2,1,%d,0,120,0", synth, (int)expo(t, 40.0f, 3000.0f));
            return true;
        case S_MIC * 2 + 1: snprintf(out, len, "i%dv0F%.0f", synth, expo(t, 300.0f, 12000.0f)); return true;
        // DRAW: a low-pass over the drawn wave, and how slowly it fades in.
        case S_DRAW * 2 + 0: snprintf(out, len, "i%dv0F%.0f", synth, expo(t, 150.0f, 14000.0f)); return true;
        case S_DRAW * 2 + 1:
            snprintf(out, len, "i%dv0A%d,1,300,0.8,%d,0", synth, (int)expo(t, 5.0f, 900.0f), (int)expo(t, 250.0f, 1200.0f));
            return true;
        // CHIP: duty in the classic steps (12.5, 25, 50%), chip vibrato.
        case S_CHIP * 2 + 0: {
            static const float duty[3] = {0.125f, 0.25f, 0.5f};
            snprintf(out, len, "i%dv0d%.3f", synth, duty[(int)(t * 2.99f)]);
            return true;
        }
        case S_CHIP * 2 + 1: snprintf(out, len, "i%dv0f,,,,,%.4f", synth, t * t * 0.08f); return true;
        // BOOM: how far it drops (octaves), how hard it's driven.
        case S_BOOM * 2 + 0: snprintf(out, len, "i%dv0f,,,,%.2f", synth, lin(t, 0.3f, 5.0f)); return true;
        case S_BOOM * 2 + 1: snprintf(out, len, "i%dv0GD%.2f", synth, expo(t, 1.0f, 10.0f)); return true;
        // ORGAN: through the registrations; the speaker's speed.
        case S_ORGAN * 2 + 0: snprintf(out, len, "i%dv0d%.3f", synth, t); return true;
        case S_ORGAN * 2 + 1: snprintf(out, len, "i%dv1f%.2f", synth, expo(t, 0.4f, 7.0f)); return true;
        // VOX: U O A E I; vibrato depth.
        case S_VOX * 2 + 0: snprintf(out, len, "i%dv0d%.3f", synth, t); return true;
        case S_VOX * 2 + 1: snprintf(out, len, "i%dv0f,,,,,%.4f", synth, t * 0.03f); return true;
        // REESE: the saws' detune (centred); the cutoff.
        case S_REESE * 2 + 0: {
            const float sp = lin(t, 0.002f, 0.04f);
            snprintf(out, len, "i%dv0C2,%.4f,%.4f,1", synth, sp, -0.5f * sp);
            return true;
        }
        case S_REESE * 2 + 1: snprintf(out, len, "i%dv0F%.1f", synth, expo(t, 150.0f, 3500.0f)); return true;
        // STRINGS: the ensemble's spread; how slowly it swells (and fades).
        case S_STRINGS * 2 + 0: {
            const float sp = lin(t, 0.0f, 0.02f);
            snprintf(out, len, "i%dv0C3,%.4f,%.4f,0.9", synth, sp, -sp);
            return true;
        }
        case S_STRINGS * 2 + 1:
            snprintf(out, len, "i%dv0A%d,1,600,0.85,%d,0", synth, (int)expo(t, 30.0f, 1500.0f), (int)expo(t, 400.0f, 1500.0f));
            return true;
        // MALLET: where it's struck (wood -> metal); how long it rings.
        case S_MALLET * 2 + 0: snprintf(out, len, "i%dv0d%.3f", synth, lin(t, 0.03f, 0.5f)); return true;
        case S_MALLET * 2 + 1: snprintf(out, len, "i%dv0b%.4f", synth, lin(t, 0.93f, 0.995f)); return true;
        // E.PIANO: how hard the tine barks (modulation); the tine's ratio.
        case S_EPIANO * 2 + 0: snprintf(out, len, "i%dv2a%.2f", synth, lin(t, 0.2f, 4.0f)); return true;
        case S_EPIANO * 2 + 1: {
            static const float ratios[4] = {1.0f, 2.0f, 3.0f, 14.0f};
            snprintf(out, len, "i%dv2I%.1f", synth, ratios[(int)(t * 3.99f)]);
            return true;
        }
        // ZAP: how far the pitch sweeps (octaves); how long it lasts.
        case S_ZAP * 2 + 0: snprintf(out, len, "i%dv0f,,,,%.2f", synth, lin(t, 0.5f, 7.0f)); return true;
        case S_ZAP * 2 + 1: snprintf(out, len, "i%dv0A0,1,%d,0,60,0", synth, (int)expo(t, 40.0f, 900.0f)); return true;
        // SAMPLE: as MIC.
        case S_SAMPLE * 2 + 0:
            if (t > 0.97f)
                snprintf(out, len, "i%dv0A2,1,30,1,250,0", synth);
            else
                snprintf(out, len, "i%dv0A2,1,%d,0,120,0", synth, (int)expo(t, 40.0f, 3000.0f));
            return true;
        case S_SAMPLE * 2 + 1: snprintf(out, len, "i%dv0F%.0f", synth, expo(t, 300.0f, 12000.0f)); return true;
        default: return false;
    }
}

int sound_mic_index(void) { return S_MIC; }
int sound_sample_index(void) { return S_SAMPLE; }
int sound_draw_index(void) { return S_DRAW; }

// ------------------------------------------------------------------ EDIT --

// What the EDIT page reaches into in each CUSTOM sound: its amp envelope
// (on osc env_osc), filter (on osc 0; `filter` false = it has none, and
// one is added when CUTOFF or RESO is touched), and the LFO: an extra osc
// (lfo_osc) added to each voice when DEPTH is touched, or, for sounds that
// already modulate with osc 1, that one (`lfo_shared`: its speed is RATE).
typedef struct {
    float a, d, s, r;     // ms, ms, level, ms
    float cut, q, fenv;   // Hz, Q, octaves
    int8_t env_osc, lfo_osc;
    bool lfo_shared, filter;
    uint8_t unison;       // copies, if the sound is already a unison cluster (else 0)
    bool uni_ok;          // its oscillators can be a unison cluster (not KS, PCM or FM)
} EditInfo;

static const EditInfo kEdit[S_COUNT] = {
    [S_JUNO] = {5, 600, 0.7f, 300, 2000, 1.5f, 2.0f, 0, -1, false, false, 0, true},
    [S_DX7] = {1, 1500, 0.5f, 400, 16000, 0.7f, 0, 0, -1, false, false, 0, false},
    [S_PLUCK] = {1, 6000, 1.0f, 900, 16000, 0.7f, 0, 0, 1, false, false, 0, false},
    [S_SQUELCH] = {3, 250, 0.45f, 120, 250, 4.0f, 3.5f, 0, 1, true, true, 0, true},
    [S_BELL] = {1, 3000, 0.0f, 600, 16000, 0.7f, 0, 1, 3, false, false, 0, false},
    [S_BREEZE] = {250, 600, 0.6f, 900, 440, 10.0f, 0, 0, 1, false, true, 0, false},
    [S_SUPERSAW] = {10, 400, 0.7f, 300, 1800, 1.2f, 2.0f, 0, 1, false, true, 5, true},
    [S_FOLD] = {5, 350, 0.6f, 250, 16000, 0.7f, 0, 0, 1, false, false, 0, true},
    [S_WAVY] = {20, 500, 0.7f, 400, 16000, 0.7f, 0, 0, 1, true, false, 0, true},
    [S_MIC] = {2, 30, 1.0f, 250, 9000, 0.8f, 0, 0, 1, false, true, 0, false},
    [S_DRAW] = {5, 300, 0.8f, 250, 7000, 0.7f, 0, 0, 1, false, true, 0, true},
    [S_CHIP] = {2, 120, 0.75f, 80, 16000, 0.7f, 0, 0, 1, true, false, 0, true},
    [S_BOOM] = {1, 700, 0.5f, 250, 16000, 0.7f, 0, 0, 1, false, false, 0, true},
    [S_ORGAN] = {8, 40, 1.0f, 60, 16000, 0.7f, 0, 0, 1, true, false, 0, true},
    [S_VOX] = {150, 300, 0.9f, 400, 16000, 0.7f, 0, 0, 1, true, false, 0, true},
    [S_REESE] = {5, 300, 0.9f, 200, 700, 1.8f, 1.0f, 0, 1, false, true, 2, true},
    [S_STRINGS] = {450, 600, 0.85f, 900, 2400, 0.7f, 0, 0, 1, false, true, 3, true},
    [S_MALLET] = {1, 3000, 0.0f, 250, 16000, 0.7f, 0, 0, 1, false, false, 0, false},
    [S_EPIANO] = {1, 3500, 0.25f, 500, 16000, 0.7f, 0, 1, 3, false, false, 0, false},
    [S_ZAP] = {1, 220, 0.0f, 60, 16000, 0.7f, 0, 0, 1, false, false, 0, true},
    [S_SAMPLE] = {2, 30, 1.0f, 250, 9000, 0.8f, 0, 0, 1, false, true, 0, false},
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int sidx(int index) { return clampi(index, 0, S_COUNT - 1); }
static bool is_set(const SynthEdit *e, EditParam p) { return (e->set >> p) & 1u; }

void sound_edit_defaults(int index, float out[ED_PARAMS]) {
    const EditInfo *k = &kEdit[sidx(index)];
    out[ED_ATTACK] = edit_norm(ED_ATTACK, k->a);
    out[ED_DECAY] = edit_norm(ED_DECAY, k->d);
    out[ED_SUSTAIN] = k->s;
    out[ED_RELEASE] = edit_norm(ED_RELEASE, k->r);
    out[ED_CUTOFF] = edit_norm(ED_CUTOFF, k->cut);
    out[ED_RESO] = edit_norm(ED_RESO, k->q);
    out[ED_FENV] = edit_norm(ED_FENV, k->fenv);
    out[ED_LFO_RATE] = edit_norm(ED_LFO_RATE, 4.0f);
    out[ED_LFO_DEPTH] = 0.0f;
    out[ED_GLIDE] = 0.0f;
    out[ED_DETUNE] = edit_norm(ED_DETUNE, k->unison ? 14.0f : 8.0f);  // (built-in clusters start at their own spread)
    out[ED_BRIGHT] = 0.5f;  // x1
}

static bool lfo_on(int index, const SynthEdit *e) {
    return kSounds[sidx(index)].kind == SOUND_CUSTOM && is_set(e, ED_LFO_DEPTH) && e->p[ED_LFO_DEPTH] > 0.01f;
}
static int unison_of(int index, const SynthEdit *e) {
    const EditInfo *k = &kEdit[sidx(index)];
    return k->unison || !k->uni_ok ? 1 : clampi(e->unison, 1, UNISON_MAX);
}

int sound_edit_voices(int index, const SynthEdit *e) {
    const int v = kSounds[sidx(index)].voices;
    if (e->mono) return 1;
    const int n = v / unison_of(index, e);
    return n < 1 ? 1 : n;
}

uint32_t sound_edit_key(int index, const SynthEdit *e) {
    const bool extra_lfo = lfo_on(index, e) && !kEdit[sidx(index)].lfo_shared;
    return (uint32_t)sound_edit_voices(index, e) | (uint32_t)unison_of(index, e) << 8 | (uint32_t)extra_lfo << 12;
}

void sound_edit_patch(int index, const SynthEdit *e, char *out, size_t len) {
    const Sound *s = &kSounds[sidx(index)];
    const EditInfo *k = &kEdit[sidx(index)];
    int n = snprintf(out, len, "%s", s->patch ? s->patch : "");
    if (lfo_on(index, e) && !k->lfo_shared && n > 0 && (size_t)n < len)
        n += snprintf(out + n, len - (size_t)n, "Zv%dw0f4,0a1,,0,0Zv0L%d", k->lfo_osc, k->lfo_osc);
}

int sound_edit_wire(int index, const SynthEdit *e, int synth, char *out, int width, int max) {
    const int i = sidx(index);
    const EditInfo *k = &kEdit[i];
    const float *p = e->p;
    int n = 0;
#define EMIT(...)                                                           \
    do {                                                                    \
        if (n < max) snprintf(out + n * width, (size_t)width, __VA_ARGS__); \
        n++;                                                                \
    } while (0)
    const bool env = is_set(e, ED_ATTACK) || is_set(e, ED_DECAY) || is_set(e, ED_SUSTAIN) || is_set(e, ED_RELEASE);
    const int at = (int)edit_real(ED_ATTACK, p[ED_ATTACK]), de = (int)edit_real(ED_DECAY, p[ED_DECAY]);
    const int re = (int)edit_real(ED_RELEASE, p[ED_RELEASE]);
    const float su = p[ED_SUSTAIN];
    // Glide: on every oscillator that carries the pitch. ("i<s>m" reaches
    // them all, which a Juno needs; DX7 and the CUSTOM sounds take it on
    // osc 0 and the rest follow.)
    if (is_set(e, ED_GLIDE)) {
        const int ms = (int)edit_real(ED_GLIDE, p[ED_GLIDE]);
        EMIT("i%dm%d", synth, ms);
        EMIT("i%dv0m%d", synth, ms);
    }
    // The amp envelope. DX7: osc 0's A is its pitch envelope, so the level
    // goes through B (amp eg1) instead, and the operators' own releases are
    // stretched to match (else they'd cut it short).
    if (env && i == S_DX7) {
        EMIT("i%dv0a1,0,1,0,1B%d,1,%d,%.3f,%d,0X0", synth, at, de, (double)su, re * 2);
        for (int op = 2; op <= 7; op++) EMIT("i%dv%dA,,,,,,,,%d,", synth, op, re * 2);
    } else if (env) {
        EMIT("i%dv%dA%d,1,%d,%.3f,%d,0", synth, k->env_osc, at, de, (double)su, re);
    }
    // Filter on osc 0 (DX7 gets one: LP24). BRIGHT moves the cutoff on the
    // filtered sounds; on DX7 it's how hard the modulators hit (engine.c).
    // Sounds without a filter get one (AMY takes the type live) as soon
    // as anything filter-ish is touched.
    const bool add = !k->filter && kSounds[i].kind == SOUND_CUSTOM &&
                     (is_set(e, ED_CUTOFF) || is_set(e, ED_RESO) || is_set(e, ED_FENV) || is_set(e, ED_BRIGHT));
    const bool cut = add || is_set(e, ED_CUTOFF) || (is_set(e, ED_BRIGHT) && i != S_DX7 && i != S_JUNO);
    if (cut) {
        float c = is_set(e, ED_CUTOFF) ? edit_real(ED_CUTOFF, p[ED_CUTOFF]) : k->cut;
        if (i != S_DX7 && i != S_JUNO && is_set(e, ED_BRIGHT)) c *= edit_real(ED_BRIGHT, p[ED_BRIGHT]);
        if (c > 16000.0f) c = 16000.0f;
        EMIT(i == S_DX7 ? "i%dv0G4F%.1f" : add ? "i%dv0G1F%.1f" : "i%dv0F%.1f", synth, (double)c);
    }
    if (is_set(e, ED_RESO)) EMIT(i == S_DX7 ? "i%dv0G4R%.2f" : "i%dv0R%.2f", synth, (double)edit_real(ED_RESO, p[ED_RESO]));
    if (is_set(e, ED_FENV) && i != S_DX7) EMIT("i%dv0F,,,%.2f", synth, (double)edit_real(ED_FENV, p[ED_FENV]));
    // LFO: its speed, and how far it moves its target (the others get 0).
    // Banks have their own LFO on osc 1; a Juno's pitch lives on oscs 2-4.
    const bool bank = i == S_JUNO || i == S_DX7;
    const int lfo = bank ? 1 : k->lfo_osc;
    const bool on = (bank || kSounds[i].kind == SOUND_CUSTOM) && is_set(e, ED_LFO_DEPTH) && p[ED_LFO_DEPTH] > 0.01f;
    if (on || (is_set(e, ED_LFO_DEPTH) && !k->lfo_shared)) {
        const float d = on ? p[ED_LFO_DEPTH] : 0.0f;
        const int t = e->lfo_target;
        if (on) EMIT("i%dv%df%.2f", synth, lfo, (double)edit_real(ED_LFO_RATE, p[ED_LFO_RATE]));
        if (on && i == S_JUNO) EMIT("i%dv1A0,1,10000,1", synth);  // no LFO delay
        if (on && i == S_DX7) EMIT("i%dv0L1", synth);
        const double pitch = t == LFO_PITCH ? d * d * 0.5f : 0.0f;  // octaves
        if (i == S_JUNO)
            for (int o = 2; o <= 4; o++) EMIT("i%dv%df,,,,,%.3f", synth, o, pitch);
        else
            EMIT("i%dv0f,,,,,%.3f", synth, pitch);
        EMIT("i%dv0F,,,,,%.3f", synth, (double)(t == LFO_FILTER ? d * 4.0f : 0.0f));  // octaves
        EMIT("i%dv%da,,,,,%.3f", synth, bank ? 0 : k->env_osc, (double)(t == LFO_AMP ? d : 0.0f));
        EMIT("i%dv0Q%s,,,,,%.3f", synth, t == LFO_PAN ? "0.5" : "", (double)(t == LFO_PAN ? d : 0.0f));
    } else if (is_set(e, ED_LFO_RATE) && (k->lfo_shared || bank)) {
        EMIT("i%dv%df%.2f", synth, lfo, (double)edit_real(ED_LFO_RATE, p[ED_LFO_RATE]));
    }
    // Unison: copies of each voice, spread by DETUNE, centred on the note.
    const int u = unison_of(i, e);
    const float sp = edit_real(ED_DETUNE, p[ED_DETUNE]) / 1200.0f;
    if (k->unison && is_set(e, ED_DETUNE)) {  // a built-in cluster: DETUNE spreads its own copies
        EMIT("i%dv0C%d,%.4f,%.4f,0.85", synth, k->unison, (double)sp, (double)(-0.5f * (float)(k->unison - 1) * sp));
    } else if (u > 1) {
        const double off = -0.5f * (float)(u - 1) * sp;
        if (i == S_JUNO)
            for (int o = 2; o <= 4; o++) EMIT("i%dv%dC%d,%.4f,%.4f,1", synth, o, u, (double)sp, off);
        else
            EMIT("i%dv0C%d,%.4f,%.4f,1", synth, u, (double)sp, off);
    }
#undef EMIT
    return n < max ? n : max;
}

float sound_edit_bright(int index, const SynthEdit *e) {
    return sidx(index) == S_DX7 && is_set(e, ED_BRIGHT) ? edit_real(ED_BRIGHT, e->p[ED_BRIGHT]) : 1.0f;
}

int sound_dx7_index(void) { return S_DX7; }

int sound_unison_kind(int index) {
    const EditInfo *k = &kEdit[sidx(index)];
    return k->unison ? 2 : k->uni_ok ? 1 : 0;
}
