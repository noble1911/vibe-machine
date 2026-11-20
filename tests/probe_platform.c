// Platform hooks for running AMY on the build machine (no audio device).
#include "amy.h"

void amy_platform_init(void) {}
void amy_platform_deinit(void) {}
void amy_update_tasks(void) {}
int16_t *amy_render_audio(void) { return amy_simple_fill_buffer(); }
size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
    (void)buffer;
    return nbytes;
}
#ifndef __linux__  // amy_midi.c already stubs these on Linux without ALSA
void run_midi(void) {}
void stop_midi(void) {}
#endif

// examples.c (unused here) calls this host sleep.
void delay_ms(uint32_t ms) { (void)ms; }
