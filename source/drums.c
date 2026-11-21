// drums.c -- see drums.h.

#include "drums.h"

#include "drum_kits.h"
#include "music.h"

static const char *const kLaneNames[DRUM_LANES] = {"KICK", "SNARE", "HAT", "OPEN", "CLAP", "RIM", "TOM", "CRASH"};
#define OWN_GAIN 4.0f  // a sample of your own (normalised to 0.9 peak)

int drum_kit_count(void) { return DRUM_KITS; }
const char *drum_kit_name(int kit) { return kit >= 0 && kit < DRUM_KITS ? kDrumKits[kit].name : "?"; }
const char *drum_lane_name(int lane) { return lane >= 0 && lane < DRUM_LANES ? kLaneNames[lane] : "?"; }

static int sample_index(uint16_t preset) {
    for (int i = 0; i < DRUM_SAMPLES; i++)
        if (kDrumSamples[i].preset == preset) return i;
    return -1;
}

static bool own(const DrumKit *k, int lane) { return k->lane[lane].preset == DRUM_USER_PRESET + lane; }

void drum_kit_stock(DrumKit *k, int kit) {
    if (kit < 0 || kit >= DRUM_KITS) kit = 0;
    k->kit = (uint8_t)kit;
    k->edited = 0;
    for (int i = 0; i < DRUM_LANES; i++) {
        k->lane[i].preset = kDrumKits[kit].lane[i].preset;
        k->lane[i].tune = kDrumKits[kit].lane[i].tune;
        k->lane[i].level = 100;
    }
}

void drum_kit_clamp(DrumKit *k) {
    if (k->kit >= DRUM_KITS) k->kit = 0;
    for (int i = 0; i < DRUM_LANES; i++) {
        DrumLane *l = &k->lane[i];
        if (!own(k, i) && sample_index(l->preset) < 0) {  // unknown: the stock kit's
            l->preset = kDrumKits[k->kit].lane[i].preset;
            l->tune = kDrumKits[k->kit].lane[i].tune;
        }
        if (l->tune > DRUM_TUNE_MAX) l->tune = DRUM_TUNE_MAX;
        if (l->tune < -DRUM_TUNE_MAX) l->tune = -DRUM_TUNE_MAX;
        if (l->level > DRUM_LEVEL_MAX) l->level = DRUM_LEVEL_MAX;
    }
}

float drum_lane_note(const DrumKit *k, int lane) {
    const int s = sample_index(k->lane[lane].preset);
    const int root = s >= 0 ? kDrumSamples[s].root : 60;
    return (float)(root + k->lane[lane].tune);
}

float drum_lane_gain(const DrumKit *k, int lane) {
    const int s = sample_index(k->lane[lane].preset);
    return (s >= 0 ? kDrumSamples[s].gain : OWN_GAIN) * k->lane[lane].level / 100.0f;
}

const char *drum_lane_sample_name(const DrumKit *k, int lane) {
    if (own(k, lane)) return "OWN SAMPLE";
    const int s = sample_index(k->lane[lane].preset);
    return s >= 0 ? kDrumSamples[s].name : "?";
}

const char *drum_lane_bank_name(const DrumKit *k, int lane) {
    if (own(k, lane)) return "FROM MIC";
    const int s = sample_index(k->lane[lane].preset);
    return s >= 0 ? kDrumBanks[kDrumSamples[s].bank] : "";
}

int drum_lane_of_note(int note) {
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        int n[MUSIC_MAX_NOTES];
        music_key_notes(lane, PLAY_DRUM, 0, n);
        if (n[0] == note) return lane;
    }
    return -1;
}

void drum_lane_step(DrumKit *k, int lane, int dir) {
    int s = sample_index(k->lane[lane].preset);
    if (s < 0) s = dir > 0 ? -1 : 0;  // from your own sample: the first / last
    s = ((s + (dir > 0 ? 1 : -1)) % DRUM_SAMPLES + DRUM_SAMPLES) % DRUM_SAMPLES;
    k->lane[lane].preset = kDrumSamples[s].preset;
    k->lane[lane].tune = 0;
    k->edited |= (uint8_t)(1u << lane);
}

void drum_lane_use_own(DrumKit *k, int lane) {
    k->lane[lane].preset = (uint16_t)(DRUM_USER_PRESET + lane);
    k->lane[lane].tune = 0;
    k->edited |= (uint8_t)(1u << lane);
}

void drum_lane_set_tune(DrumKit *k, int lane, int semis) {
    k->lane[lane].tune = (int8_t)(semis > DRUM_TUNE_MAX ? DRUM_TUNE_MAX : semis < -DRUM_TUNE_MAX ? -DRUM_TUNE_MAX : semis);
    k->edited |= (uint8_t)(1u << lane);
}

void drum_lane_set_level(DrumKit *k, int lane, int percent) {
    k->lane[lane].level = (uint8_t)(percent > DRUM_LEVEL_MAX ? DRUM_LEVEL_MAX : percent < 0 ? 0 : percent);
    k->edited |= (uint8_t)(1u << lane);
}
