// dual_probe -- AMY's two-core rendering, as the 3DS build uses it: the
// oscillators split between two threads at a voice boundary must sound
// exactly like one thread rendering them all.   (built against the copy of
// AMY that tools/amy-dualcore.sh makes)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amy.h"
#include "amy_split.h"
#include "mixer.h"
#include "mix_setup.h"
#include "sounds.h"

#ifndef AMY_DUALCORE
#error "dual_probe needs AMY built with VM_AMY_DUALCORE"
#endif

static pthread_t s_worker;
static pthread_mutex_t s_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_c = PTHREAD_COND_INITIALIZER;
static int s_job = -1, s_done;  // split point for the worker; done flag

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&s_m);
        while (s_job < 0) pthread_cond_wait(&s_c, &s_m);
        int split = s_job;
        pthread_mutex_unlock(&s_m);
        if (split > AMY_OSCS) return NULL;
        amy_render((uint16_t)split, AMY_OSCS, 1);
        pthread_mutex_lock(&s_m);
        s_job = -1;
        s_done = 1;
        pthread_cond_broadcast(&s_c);
        pthread_mutex_unlock(&s_m);
    }
}

// One block, the engine.c way.
static const int16_t *block(int split_mode, int *split_seen) {
    amy_execute_deltas();
    if (split_mode) {
        uint16_t split = amy_split_point(0.6f);
        *split_seen = split;
        pthread_mutex_lock(&s_m);
        s_done = 0;
        s_job = split;
        pthread_cond_broadcast(&s_c);
        pthread_mutex_unlock(&s_m);
        amy_render(0, split, 0);
        pthread_mutex_lock(&s_m);
        while (!s_done) pthread_cond_wait(&s_c, &s_m);
        pthread_mutex_unlock(&s_m);
    } else {
        amy_render(0, AMY_OSCS, 0);
        amy_render(AMY_OSCS, AMY_OSCS, 1);  // nothing: just clears core 1's buffers
    }
    return amy_fill_buffer();
}

static void wire(const char *m) {
    char b[512];
    snprintf(b, sizeof(b), "%s", m);
    amy_add_message(b);
}
static void note(int synth, int n, float v) {
    amy_event e = amy_default_event();
    e.synth = (uint8_t)synth;
    e.midi_note = (float)n;
    e.velocity = v;
    amy_add_event(&e);
}

#define BLOCKS 1500
static int16_t s_out[BLOCKS * AMY_BLOCK_SIZE * 2];

static void scene(int split_mode, int *splits_min, int *splits_max) {
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_NONE;
    c.midi = AMY_MIDI_IS_NONE;
    c.platform.multicore = 0;
    c.platform.multithread = 0;
    c.features.default_synths = 1;
    c.max_buses = MIX_BUSES;
    c.amy_external_bus_postprocess_hook = mixer_hook;
    amy_start(c);
    mix_setup(0.2f, 0.3f);
    static MixStrip st[MIX_STRIPS];
    for (int k = 0; k < MIX_STRIPS; k++) {
        mixer_strip_default(&st[k]);
        mixer_set_gain(k, &st[k], true);
    }
    // Part 1 JUNO, part 2 DX7, part 3 SUPERSAW; drums on synth 10.
    amy_event e = amy_default_event();
    e.synth = 1;
    e.num_voices = 6;
    e.patch_number = 0;
    amy_add_event(&e);
    e.synth = 2;
    e.patch_number = 130;
    amy_add_event(&e);
    char b[512];
    snprintf(b, sizeof(b), "i3iv4u%s", sound_get(6)->patch);
    wire(b);
    for (int k = 0; k < 3; k++) {
        amy_event r = amy_default_event();
        r.synth = (uint8_t)(k + 1);
        r.bus = (uint16_t)k;
        amy_add_event(&r);
    }
    int mn = AMY_OSCS, mx = 0;
    for (int i = 0; i < BLOCKS; i++) {
        if (i == 10) { note(1, 60, 0.8f); note(1, 64, 0.8f); note(1, 67, 0.8f); }
        if (i == 30) { note(2, 72, 0.8f); note(2, 76, 0.8f); }
        if (i == 50) { note(3, 48, 0.8f); note(3, 55, 0.8f); }
        if (i % 40 == 0) note(10, 36 + (i / 40) % 8, 1.0f);
        if (i == 700) { note(1, 60, 0); note(1, 64, 0); note(1, 67, 0); note(2, 72, 0); }
        int sp = AMY_OSCS;
        const int16_t *blk = block(split_mode, &sp);
        if (split_mode) {
            if (sp < mn) mn = sp;
            if (sp > mx) mx = sp;
        }
        memcpy(s_out + i * AMY_BLOCK_SIZE * 2, blk, sizeof(int16_t) * AMY_BLOCK_SIZE * 2);
    }
    *splits_min = mn;
    *splits_max = mx;
    amy_stop();
}

int main(int argc, char **argv) {
    if (argc > 1) {  // child: render one way, write it out
        int a, b;
        if (atoi(argv[1])) pthread_create(&s_worker, NULL, worker, NULL);
        scene(atoi(argv[1]), &a, &b);
        FILE *f = fopen(argv[2], "wb");
        fwrite(s_out, sizeof(s_out), 1, f);
        fclose(f);
        if (atoi(argv[1])) printf("  split points ranged %d..%d (of %d oscs)\n", a, b, AMY_OSCS);
        return 0;
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s 0 build/dual_single.raw && %s 1 build/dual_split.raw", argv[0], argv[0]);
    if (system(cmd) != 0) return 1;
    static int16_t a[sizeof(s_out) / 2], b[sizeof(s_out) / 2];
    FILE *fa = fopen("build/dual_single.raw", "rb"), *fb = fopen("build/dual_split.raw", "rb");
    size_t na = fread(a, 1, sizeof(a), fa), nb = fread(b, 1, sizeof(b), fb);
    fclose(fa);
    fclose(fb);
    long diff = 0, loud = 0;
    for (size_t i = 0; i < sizeof(a) / 2; i++) {
        diff += a[i] != b[i];
        loud += abs(a[i]) > 1000;
    }
    const int ok = na == nb && na == sizeof(a) && diff == 0 && loud > 1000;
    printf("%-58s %s  (%ld of %zu samples differ, %ld loud)\n", "two cores render exactly what one core does", ok ? "ok" : "FAIL",
           diff, sizeof(a) / 2, loud);
    printf(ok ? "\nall dual-core OK\n" : "\ndual-core FAILED\n");
    return ok ? 0 : 1;
}
