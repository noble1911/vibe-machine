// audio_out.c -- see audio_out.h.
//
// Design: one ndsp channel plays stereo PCM16 from a ring of wave buffers.
// ndsp calls our callback once per DSP frame (~5 ms); the callback only
// signals an event, and a dedicated high-priority thread does the refill so
// AMY never runs inside ndsp's own thread.

#include "audio_out.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "fx.h"
#include "mixer.h"
#include "punch.h"
#include "seq.h"
#include "tape.h"

_Static_assert(TAPE_SOURCES == MIX_STRIPS, "each MIX strip is a tape source");

#define CHANNEL 0
#define NUM_WAVEBUFS 4
#define BLOCKS_PER_WAVEBUF 4  // 4 x 128-frame AMY blocks = 512 frames
#define AUDIO_STACK_SIZE (128 * 1024)

static ndspWaveBuf s_wavebufs[NUM_WAVEBUFS];
static int16_t *s_audio_mem;
static int s_frames_per_wavebuf;

static Thread s_thread;
static LightEvent s_event;
static volatile bool s_quit;
static bool s_running;

static LightLock s_stats_lock;
static AudioStats s_stats;
static int16_t s_scope[SCOPE_POINTS];
static uint64_t s_peak_window_start;

// Mixdown capture ring: the audio thread writes, the main thread reads.
#define CAP_FRAMES (44100 * 3)
static int16_t *s_cap;
static uint32_t s_cap_w, s_cap_r;  // frames written / read, ever (wrap is fine: unsigned)
static volatile bool s_capturing;
static volatile uint32_t s_cap_dropped;

void audio_out_capture(bool on) {
    if (on && !s_cap) s_cap = (int16_t *)malloc(CAP_FRAMES * 2 * sizeof(int16_t));
    if (on && !s_cap) return;
    if (on && !s_capturing) {  // starting: from now on (already on: carry on)
        __atomic_store_n(&s_cap_r, __atomic_load_n(&s_cap_w, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        s_cap_dropped = 0;
    }
    s_capturing = on;
}

static void capture_block(const int16_t *block, int frames) {
    if (!s_capturing || !s_cap) return;
    const uint32_t w = __atomic_load_n(&s_cap_w, __ATOMIC_RELAXED), r = __atomic_load_n(&s_cap_r, __ATOMIC_ACQUIRE);
    if (CAP_FRAMES - (w - r) < (uint32_t)frames) {
        s_cap_dropped += (uint32_t)frames;
        return;
    }
    for (int i = 0; i < frames; i++) {
        const uint32_t k = (w + (uint32_t)i) % CAP_FRAMES;
        s_cap[2 * k] = block[2 * i];
        s_cap[2 * k + 1] = block[2 * i + 1];
    }
    __atomic_store_n(&s_cap_w, w + (uint32_t)frames, __ATOMIC_RELEASE);
}

int audio_out_capture_read(int16_t *out, int max_frames) {
    if (!s_cap) return 0;
    const uint32_t r = __atomic_load_n(&s_cap_r, __ATOMIC_RELAXED), w = __atomic_load_n(&s_cap_w, __ATOMIC_ACQUIRE);
    uint32_t n = w - r;
    if (n > (uint32_t)max_frames) n = (uint32_t)max_frames;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t k = (r + i) % CAP_FRAMES;
        out[2 * i] = s_cap[2 * k];
        out[2 * i + 1] = s_cap[2 * k + 1];
    }
    __atomic_store_n(&s_cap_r, r + n, __ATOMIC_RELEASE);
    return (int)n;
}

uint32_t audio_out_capture_dropped(void) { return s_cap_dropped; }

static void ndsp_callback(void *arg) {
    (void)arg;
    LightEvent_Signal(&s_event);
}

static void record_block(const int16_t *block, int frames, uint64_t ticks) {
    const double block_ticks = (double)frames / engine_sample_rate() * SYSCLOCK_ARM11;
    float load = (float)(ticks / block_ticks);

    int peak = 0;
    for (int i = 0; i < frames * 2; i++) {
        int v = block[i] < 0 ? -block[i] : block[i];
        if (v > peak) peak = v;
    }

    LightLock_Lock(&s_stats_lock);
    s_stats.load = s_stats.load * 0.95f + load * 0.05f;
    uint64_t now = svcGetSystemTick();
    if (now - s_peak_window_start > SYSCLOCK_ARM11) {
        s_peak_window_start = now;
        s_stats.load_peak = load;
    } else if (load > s_stats.load_peak) {
        s_stats.load_peak = load;
    }
    float level = peak / 32768.0f;
    s_stats.level = level > s_stats.level ? level : s_stats.level * 0.92f;
    s_stats.blocks++;
    // Rolling window of the newest SCOPE_POINTS frames (mono mix).
    int n = frames < SCOPE_POINTS ? frames : SCOPE_POINTS;
    memmove(s_scope, s_scope + n, (SCOPE_POINTS - n) * sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        const int16_t *f = block + 2 * (frames - n + i);
        s_scope[SCOPE_POINTS - n + i] = (int16_t)((f[0] + f[1]) / 2);
    }
    LightLock_Unlock(&s_stats_lock);
}

static void fill_wavebuf(ndspWaveBuf *wb) {
    int16_t *dst = wb->data_pcm16;
    const int frames = engine_block_frames();
    for (int b = 0; b < BLOCKS_PER_WAVEBUF; b++) {
        // The sequencer clock runs here, on the sample clock: steps that fall
        // due are sent to the engine just before the block that plays them.
        seq_advance(frames, engine_sample_rate());
        const SeqClock clk = seq_clock();  // PUNCH engages on the 16ths the sequencer fires
        punch_clock(clk.fired, clk.step, clk.playing, clk.punch, (float)clk.bpm);
        uint64_t t0 = svcGetSystemTick();
        const int16_t *block = engine_render_block();
        uint64_t t1 = svcGetSystemTick();
        memcpy(dst, block, (size_t)frames * 2 * sizeof(int16_t));
        tape_play_block(dst, frames, engine_sample_rate());  // the tape plays along...
        punch_process(dst, frames);                          // ...PUNCH-ins and the performance FX go
        fx_process(dst, frames);                             // over everything...
        const float *strips[TAPE_SOURCES];  // (each MIX strip on its own, for tracks that record one)
        for (int k = 0; k < TAPE_SOURCES; k++) strips[k] = mixer_strip_block(k);
        tape_record_block(dst, strips, frames);              // ...and the armed track records
        capture_block(dst, frames);                          // the mixdown, if one is running
        record_block(dst, frames, t1 - t0);
#ifdef VM_DEMO
        memset(dst, 0, (size_t)frames * 2 * sizeof(int16_t));  // attract mode: scope moves, speakers stay quiet
#endif
        dst += frames * 2;
    }
    DSP_FlushDataCache(wb->data_pcm16, (u32)s_frames_per_wavebuf * 2 * sizeof(int16_t));
    ndspChnWaveBufAdd(CHANNEL, wb);
}

static void audio_thread(void *arg) {
    (void)arg;
    while (!s_quit) {
        int queued = 0;
        for (int i = 0; i < NUM_WAVEBUFS; i++) {
            u8 st = s_wavebufs[i].status;
            if (st == NDSP_WBUF_FREE || st == NDSP_WBUF_DONE) {
                fill_wavebuf(&s_wavebufs[i]);
            } else {
                queued++;
            }
        }
        // If nothing was still queued, the DSP had drained completely: an
        // audible gap. Count it so the UI can show it.
        if (queued == 0 && s_stats.blocks > NUM_WAVEBUFS * BLOCKS_PER_WAVEBUF) {
            LightLock_Lock(&s_stats_lock);
            s_stats.late++;
            LightLock_Unlock(&s_stats_lock);
        }
        LightEvent_Wait(&s_event);
    }
}

bool audio_out_init(void) {
    LightLock_Init(&s_stats_lock);
    LightEvent_Init(&s_event, RESET_ONESHOT);
    s_peak_window_start = svcGetSystemTick();

    fx_init(engine_sample_rate());
    punch_init(engine_sample_rate());
    if (R_FAILED(ndspInit())) return false;

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(CHANNEL);
    ndspChnSetInterp(CHANNEL, NDSP_INTERP_LINEAR);
    // AMY renders at 44.1 kHz; the DSP resamples to its native ~32.7 kHz.
    ndspChnSetRate(CHANNEL, (float)engine_sample_rate());
    ndspChnSetFormat(CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {0};
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(CHANNEL, mix);

    s_frames_per_wavebuf = engine_block_frames() * BLOCKS_PER_WAVEBUF;
    size_t bytes = (size_t)s_frames_per_wavebuf * 2 * sizeof(int16_t) * NUM_WAVEBUFS;
    s_audio_mem = (int16_t *)linearAlloc(bytes);
    if (!s_audio_mem) {
        ndspExit();
        return false;
    }
    memset(s_audio_mem, 0, bytes);
    memset(s_wavebufs, 0, sizeof(s_wavebufs));
    for (int i = 0; i < NUM_WAVEBUFS; i++) {
        s_wavebufs[i].data_pcm16 = s_audio_mem + i * s_frames_per_wavebuf * 2;
        s_wavebufs[i].nsamples = (u32)s_frames_per_wavebuf;
    }

    ndspSetCallback(ndsp_callback, NULL);

    // Audio outranks the UI on the app core (lower number = higher priority),
    // so drawing can never starve the synth.
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_quit = false;
    s_thread = threadCreate(audio_thread, NULL, AUDIO_STACK_SIZE, prio - 1, -2, false);
    if (!s_thread) {
        ndspExit();
        linearFree(s_audio_mem);
        s_audio_mem = NULL;
        return false;
    }
    s_running = true;
    return true;
}

void audio_out_exit(void) {
    if (!s_running) return;
    s_quit = true;
    LightEvent_Signal(&s_event);
    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    ndspChnReset(CHANNEL);
    ndspExit();
    linearFree(s_audio_mem);
    s_audio_mem = NULL;
    s_running = false;
}

AudioStats audio_out_stats(void) {
    LightLock_Lock(&s_stats_lock);
    AudioStats s = s_stats;
    LightLock_Unlock(&s_stats_lock);
    return s;
}

void audio_out_scope(int16_t out[SCOPE_POINTS]) {
    LightLock_Lock(&s_stats_lock);
    memcpy(out, s_scope, sizeof(s_scope));
    LightLock_Unlock(&s_stats_lock);
}
