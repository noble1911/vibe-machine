// tape.c -- see tape.h.

#include "tape.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int16_t *s_track[TAPE_TRACKS];
static uint8_t s_peaks[TAPE_TRACKS][TAPE_PEAKS];  // 0..255 per overview block
static volatile bool s_used[TAPE_TRACKS];
static volatile bool s_dirty[TAPE_TRACKS];
static volatile int32_t s_len[TAPE_TRACKS];  // furthest frame written + 1
static volatile uint32_t s_gen;

uint32_t tape_generation(void) { return s_gen; }

static volatile bool s_playing, s_rec_armed, s_scrubbing;
static volatile int s_armed_track;
static volatile float s_speed = 1.0f, s_scrub_speed;
static volatile bool s_muted[TAPE_TRACKS];
static volatile bool s_rewind;

static double s_head;          // tape position in tape frames
static float s_cur_speed;      // slewed speed actually applied
static float s_level[TAPE_TRACKS];
static float s_rec_acc;        // decimation: sum of input frames for one tape frame
static int s_rec_n;
static bool s_ok;

bool tape_init(void) {
    for (int t = 0; t < TAPE_TRACKS; t++) {
        s_track[t] = calloc(TAPE_FRAMES, sizeof(int16_t));
        if (!s_track[t]) {
            tape_exit();
            return false;
        }
    }
    memset(s_peaks, 0, sizeof(s_peaks));
    s_head = 0.0;
    s_ok = true;
    return true;
}

void tape_exit(void) {
    s_ok = false;
    for (int t = 0; t < TAPE_TRACKS; t++) {
        free(s_track[t]);
        s_track[t] = NULL;
    }
}

void tape_play(bool on) { s_playing = on; }
void tape_record(bool on) { s_rec_armed = on; }
void tape_select(int track) {
    if (track >= 0 && track < TAPE_TRACKS) s_armed_track = track;
}
void tape_rewind(void) { s_rewind = true; }
void tape_set_speed(float speed) { s_speed = speed < -2.0f ? -2.0f : speed > 2.0f ? 2.0f : speed; }
void tape_scrub(bool active, float speed) {
    s_scrub_speed = speed < -8.0f ? -8.0f : speed > 8.0f ? 8.0f : speed;
    s_scrubbing = active;
}
void tape_mute(int track, bool muted) {
    if (track >= 0 && track < TAPE_TRACKS) s_muted[track] = muted;
}

void tape_clear(int track) {
    if (!s_ok || track < 0 || track >= TAPE_TRACKS) return;
    memset(s_track[track], 0, TAPE_FRAMES * sizeof(int16_t));
    memset(s_peaks[track], 0, TAPE_PEAKS);
    s_used[track] = false;
    s_len[track] = 0;
    s_dirty[track] = true;
    s_gen++;
}

bool tape_dirty(int track) { return track >= 0 && track < TAPE_TRACKS && s_dirty[track]; }

const int16_t *tape_track_data(int track, int *frames) {
    if (!s_ok || track < 0 || track >= TAPE_TRACKS) return NULL;
    *frames = s_len[track];
    return s_track[track];
}

void tape_set_dirty(int track) {
    if (track >= 0 && track < TAPE_TRACKS) s_dirty[track] = true;
}

void tape_mark_saved(int track) {
    if (track >= 0 && track < TAPE_TRACKS) s_dirty[track] = false;
}

void tape_load_track(int track, const int16_t *data, int frames) {
    if (!s_ok || track < 0 || track >= TAPE_TRACKS) return;
    if (frames > TAPE_FRAMES) frames = TAPE_FRAMES;
    memset(s_track[track], 0, TAPE_FRAMES * sizeof(int16_t));
    memcpy(s_track[track], data, (size_t)frames * sizeof(int16_t));
    memset(s_peaks[track], 0, TAPE_PEAKS);
    for (int i = 0; i < frames; i++) {
        float a = sqrtf(fabsf(data[i] / 32768.0f));
        uint8_t pk = (uint8_t)fminf(255.0f, a * 255.0f);
        int b = i / TAPE_PEAK_BLOCK;
        if (pk > s_peaks[track][b]) s_peaks[track][b] = pk;
    }
    s_len[track] = frames;
    s_used[track] = frames > 0;
    s_dirty[track] = false;
    s_gen++;
}

TapeStatus tape_status(void) {
    TapeStatus st;
    memset(&st, 0, sizeof(st));
    st.ok = s_ok;
    st.playing = s_playing;
    st.recording = s_playing && s_rec_armed;
    st.scrubbing = s_scrubbing;
    st.track = s_armed_track;
    st.seconds = (float)(s_head / TAPE_RATE);
    st.speed = s_cur_speed;
    for (int t = 0; t < TAPE_TRACKS; t++) {
        st.muted[t] = s_muted[t];
        st.level[t] = s_level[t];
        st.used[t] = s_used[t];
    }
    return st;
}

float tape_peak(int track, int block) {
    if (track < 0 || track >= TAPE_TRACKS) return 0.0f;
    block %= TAPE_PEAKS;
    if (block < 0) block += TAPE_PEAKS;
    return s_peaks[track][block] / 255.0f;
}

static float read_track(int t, double pos) {
    int32_t i0 = (int32_t)floor(pos);
    float frac = (float)(pos - i0);
    i0 %= TAPE_FRAMES;
    if (i0 < 0) i0 += TAPE_FRAMES;
    int32_t i1 = (i0 + 1) % TAPE_FRAMES;
    return (s_track[t][i0] * (1.0f - frac) + s_track[t][i1] * frac) / 32768.0f;
}

// Linear up to 0.8, then a smooth knee into 1.0, so four full tracks plus
// live play never wrap around.
static float soft(float v) {
    float a = fabsf(v);
    if (a <= 0.8f) return v;
    float k = 0.8f + 0.2f * tanhf((a - 0.8f) / 0.2f);
    return v < 0 ? -k : k;
}

void tape_process(int16_t *io, int frames, int rate) {
    if (!s_ok) return;
    if (s_rewind) {
        s_rewind = false;
        s_head = 0.0;
    }
    // Idle tape: the live sound passes through untouched.
    if (!s_playing && !s_scrubbing && fabsf(s_cur_speed) < 0.001f) {
        s_cur_speed = 0.0f;
        for (int t = 0; t < TAPE_TRACKS; t++) s_level[t] *= 0.9f;
        return;
    }
    const bool recording = s_playing && s_rec_armed && !s_scrubbing;
    const int rt = s_armed_track;
    float target = s_scrubbing ? s_scrub_speed : !s_playing ? 0.0f : recording ? 1.0f : s_speed;
    const float step = (float)TAPE_RATE / (float)rate;  // tape frames per output frame at speed 1
    const int decim = rate / TAPE_RATE > 0 ? rate / TAPE_RATE : 1;
    float lvl[TAPE_TRACKS] = {0};

    for (int n = 0; n < frames; n++) {
        // Speed eases towards its target: starts and stops sound like a
        // motor spinning up rather than a click.
        s_cur_speed += (target - s_cur_speed) * (s_scrubbing ? 0.02f : 0.004f);

        float live = (io[2 * n] + io[2 * n + 1]) * 0.5f / 32768.0f;
        if (recording) {
            s_rec_acc += live;
            if (++s_rec_n >= decim) {
                int32_t w = (int32_t)s_head % TAPE_FRAMES;
                float v = s_rec_acc / s_rec_n;
                s_track[rt][w] = (int16_t)(soft(v) * 32767.0f);
                int b = w / TAPE_PEAK_BLOCK;
                // Square-root scale, so quiet takes still read on screen.
                uint8_t pk = (uint8_t)fminf(255.0f, sqrtf(fabsf(v)) * 255.0f);
                if (w % TAPE_PEAK_BLOCK == 0) s_peaks[rt][b] = pk;  // first write of a block resets it
                else if (pk > s_peaks[rt][b]) s_peaks[rt][b] = pk;
                s_used[rt] = true;
                s_dirty[rt] = true;
                if (w + 1 > s_len[rt]) s_len[rt] = w + 1;
                s_rec_acc = 0.0f;
                s_rec_n = 0;
            }
        }

        float mix = 0.0f;
        if (fabsf(s_cur_speed) > 0.001f) {
            for (int t = 0; t < TAPE_TRACKS; t++) {
                if (s_muted[t] || !s_used[t] || (recording && t == rt)) continue;
                float v = read_track(t, s_head);
                mix += v * 0.8f;
                float a = fabsf(v);
                if (a > lvl[t]) lvl[t] = a;
            }
            // The tape's level drops as it slows to a halt.
            float sp = fabsf(s_cur_speed);
            if (sp < 0.25f) mix *= sp / 0.25f;
        }
        for (int c = 0; c < 2; c++) {
            float v = io[2 * n + c] / 32768.0f + mix;
            io[2 * n + c] = (int16_t)(soft(v) * 32767.0f);
        }

        s_head += s_cur_speed * step;
        if (s_head >= TAPE_FRAMES) s_head -= TAPE_FRAMES;
        if (s_head < 0.0) s_head += TAPE_FRAMES;
    }
    for (int t = 0; t < TAPE_TRACKS; t++) s_level[t] = lvl[t] > s_level[t] ? lvl[t] : s_level[t] * 0.9f;
    if (recording) s_gen++;
}
