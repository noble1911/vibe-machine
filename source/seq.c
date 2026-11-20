// seq.c -- see seq.h.

#include "seq.h"

#include <3ds.h>
#include <string.h>

#include "engine.h"

#define GATE 0.55  // synth notes sound for this fraction of a step
#define MAX_EVENTS 32

typedef struct {
    int synth, note;
    bool on;
} Event;

static LightLock s_lock;
static SeqPattern s_pat;
// Hits recorded slightly early (into the coming step) already sounded live;
// skip them once when that step fires so they are not heard twice.
static uint8_t s_skip_drums[SEQ_STEPS];
static bool s_skip_synth[SEQ_STEPS];

static bool s_playing, s_recording, s_start_pending;
static int s_bpm = 112;
static int s_step;
static double s_pos;  // samples since the current step fired
static int s_rate = 44100;
static uint32_t s_fired;
static SeqNote s_gate;  // synth notes currently held by the sequencer
static bool s_gate_open;

static double step_len(void) { return s_rate * 60.0 / s_bpm / 4.0; }

static int drum_note(int lane) {
    int notes[MUSIC_MAX_NOTES];
    music_key_notes(lane, PLAY_DRUM, 0, notes);
    return notes[0];
}

static int close_gate(Event *ev, int n) {
    if (!s_gate_open) return n;
    for (int i = 0; i < s_gate.count && n < MAX_EVENTS; i++) ev[n++] = (Event){SYNTH_LEAD, s_gate.notes[i], false};
    s_gate_open = false;
    return n;
}

static int fire_step(int step, Event *ev, int n) {
    n = close_gate(ev, n);
    uint8_t hits = s_pat.drums[step] & (uint8_t)~s_skip_drums[step];
    s_skip_drums[step] = 0;
    for (int lane = 0; lane < SEQ_LANES; lane++)
        if ((hits & (1u << lane)) && n < MAX_EVENTS) ev[n++] = (Event){SYNTH_DRUMS, drum_note(lane), true};

    const SeqNote *sn = &s_pat.synth[step];
    if (sn->count && !s_skip_synth[step]) {
        for (int i = 0; i < sn->count && n < MAX_EVENTS; i++) ev[n++] = (Event){SYNTH_LEAD, sn->notes[i], true};
        s_gate = *sn;
        s_gate_open = true;
    }
    s_skip_synth[step] = false;
    s_step = step;
    s_fired++;
    return n;
}

void seq_init(void) {
    LightLock_Init(&s_lock);
    memset(&s_pat, 0, sizeof(s_pat));
    // A starter groove so START makes music straight away:
    // kick on the beat, snare on 2 and 4, closed hats on the off-beats.
    for (int s = 0; s < SEQ_STEPS; s += 4) s_pat.drums[s] |= 1u << 0;
    s_pat.drums[4] |= 1u << 1;
    s_pat.drums[12] |= 1u << 1;
    for (int s = 2; s < SEQ_STEPS; s += 4) s_pat.drums[s] |= 1u << 2;
    s_pat.drums[14] |= 1u << 3;
}

void seq_advance(int frames, int rate) {
    Event ev[MAX_EVENTS];
    int n = 0;

    LightLock_Lock(&s_lock);
    s_rate = rate;
    if (s_playing) {
        if (s_start_pending) {
            s_start_pending = false;
            s_pos = 0.0;
            n = fire_step(0, ev, n);
        } else {
            const double len = step_len();
            s_pos += frames;
            if (s_gate_open && s_pos >= len * GATE) n = close_gate(ev, n);
            while (s_pos >= len) {
                s_pos -= len;
                n = fire_step((s_step + 1) % SEQ_STEPS, ev, n);
            }
        }
    } else {
        n = close_gate(ev, n);  // stopped: let go of anything still held
    }
    LightLock_Unlock(&s_lock);

    // Talk to the engine outside our lock.
    for (int i = 0; i < n; i++) {
        if (ev[i].on)
            engine_note_on(ev[i].synth, ev[i].note, ev[i].synth == SYNTH_DRUMS ? 0.95f : 0.75f);
        else
            engine_note_off(ev[i].synth, ev[i].note);
    }
}

void seq_toggle_play(void) {
    LightLock_Lock(&s_lock);
    s_playing = !s_playing;
    if (s_playing) s_start_pending = true;
    memset(s_skip_drums, 0, sizeof(s_skip_drums));
    memset(s_skip_synth, 0, sizeof(s_skip_synth));
    LightLock_Unlock(&s_lock);
}

void seq_set_recording(bool on) {
    LightLock_Lock(&s_lock);
    s_recording = on;
    LightLock_Unlock(&s_lock);
}

void seq_set_bpm(int bpm) {
    if (bpm < SEQ_BPM_MIN) bpm = SEQ_BPM_MIN;
    if (bpm > SEQ_BPM_MAX) bpm = SEQ_BPM_MAX;
    LightLock_Lock(&s_lock);
    s_bpm = bpm;
    LightLock_Unlock(&s_lock);
}

void seq_clear(void) {
    LightLock_Lock(&s_lock);
    memset(&s_pat, 0, sizeof(s_pat));
    LightLock_Unlock(&s_lock);
}

void seq_set_pattern(const SeqPattern *p) {
    LightLock_Lock(&s_lock);
    s_pat = *p;
    LightLock_Unlock(&s_lock);
}

void seq_set_drum(int step, int lane, bool on) {
    if (step < 0 || step >= SEQ_STEPS || lane < 0 || lane >= SEQ_LANES) return;
    LightLock_Lock(&s_lock);
    if (on)
        s_pat.drums[step] |= (uint8_t)(1u << lane);
    else
        s_pat.drums[step] &= (uint8_t)~(1u << lane);
    LightLock_Unlock(&s_lock);
}

void seq_set_synth(int step, const SeqNote *note) {
    if (step < 0 || step >= SEQ_STEPS) return;
    LightLock_Lock(&s_lock);
    if (note && note->count)
        s_pat.synth[step] = *note;
    else
        memset(&s_pat.synth[step], 0, sizeof(SeqNote));
    LightLock_Unlock(&s_lock);
}

// Nearest step to "now"; *ahead is set when that is the coming step.
static int nearest_step(bool *ahead) {
    *ahead = s_pos >= step_len() / 2.0;
    return *ahead ? (s_step + 1) % SEQ_STEPS : s_step;
}

void seq_record_drum(int lane) {
    if (lane < 0 || lane >= SEQ_LANES) return;
    LightLock_Lock(&s_lock);
    if (s_playing && s_recording && !s_start_pending) {
        bool ahead;
        int step = nearest_step(&ahead);
        s_pat.drums[step] |= (uint8_t)(1u << lane);
        if (ahead) s_skip_drums[step] |= (uint8_t)(1u << lane);
    }
    LightLock_Unlock(&s_lock);
}

void seq_record_synth(const SeqNote *note) {
    if (!note || !note->count) return;
    LightLock_Lock(&s_lock);
    if (s_playing && s_recording && !s_start_pending) {
        bool ahead;
        int step = nearest_step(&ahead);
        s_pat.synth[step] = *note;
        if (ahead) s_skip_synth[step] = true;
    }
    LightLock_Unlock(&s_lock);
}

SeqStatus seq_status(void) {
    LightLock_Lock(&s_lock);
    SeqStatus st = {s_playing, s_recording, s_bpm, s_step, s_fired};
    LightLock_Unlock(&s_lock);
    return st;
}

void seq_snapshot(SeqPattern *out) {
    LightLock_Lock(&s_lock);
    *out = s_pat;
    LightLock_Unlock(&s_lock);
}
