// Platform hooks AMY expects from its audio layer.
//
// On desktop these come from libminiaudio-audio.c and on microcontrollers
// from i2s.c. Clamshell owns audio output itself (see audio_out.c), so AMY
// only needs to render blocks on request; every hook here is a no-op or a
// thin pass-through.

#include "amy.h"

void amy_platform_init(void) {}

void amy_platform_deinit(void) {}

void amy_update_tasks(void) {}

int16_t *amy_render_audio(void) { return amy_simple_fill_buffer(); }

size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
    (void)buffer;
    return nbytes;
}

// amy_midi.c only defines these for platforms with a MIDI device layer.
// Clamshell sends notes to AMY directly, so there is no MIDI port to run.
void run_midi(void) {}

void stop_midi(void) {}
