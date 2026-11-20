// mic.c -- see mic.h. Follows devkitPro's mic example: the MIC service
// fills a shared ring buffer; we copy new bytes out each frame.

#include "mic.h"

#include <3ds.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "sampler.h"

#define RING_SIZE 0x30000

static u8 *s_ring;
static u32 s_ring_data, s_ring_pos;
static bool s_ok, s_recording;
static int16_t s_take[SAMPLER_MAX_FRAMES];
static int s_frames;
static float s_level;

bool mic_init(void) {
    s_ring = memalign(0x1000, RING_SIZE);
    if (!s_ring) return false;
    if (R_FAILED(micInit(s_ring, RING_SIZE))) {
        free(s_ring);
        s_ring = NULL;
        return false;
    }
    s_ring_data = micGetSampleDataSize();
    s_ok = true;
    return true;
}

void mic_exit(void) {
    if (!s_ok) return;
    if (s_recording) MICU_StopSampling();
    micExit();
    free(s_ring);
    s_ok = false;
}

void mic_start(void) {
    if (!s_ok || s_recording) return;
    s_frames = 0;
    s_level = 0.0f;
    s_ring_pos = 0;
    if (R_SUCCEEDED(MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_16360, 0, s_ring_data, true)))
        s_recording = true;
}

bool mic_poll(void) {
    if (!s_recording) return false;
    u32 end = micGetLastSampleOffset() & ~1u;  // whole 16-bit samples only
    int peak = 0;
    while (s_ring_pos != end && s_frames < SAMPLER_MAX_FRAMES) {
        int16_t v;
        memcpy(&v, s_ring + s_ring_pos, 2);
        s_take[s_frames++] = v;
        peak = abs(v) > peak ? abs(v) : peak;
        s_ring_pos = (s_ring_pos + 2) % s_ring_data;
    }
    float lvl = peak / 32768.0f;
    s_level = lvl > s_level ? lvl : s_level * 0.85f;
    return s_frames < SAMPLER_MAX_FRAMES;
}

int16_t *mic_stop(int *frames) {
    if (s_recording) {
        mic_poll();
        MICU_StopSampling();
        s_recording = false;
    }
    *frames = s_frames;
    return s_take;
}

bool mic_recording(void) { return s_recording; }
float mic_level(void) { return s_level; }
int mic_frames(void) { return s_frames; }
