// seq.c -- see seq.h.

#include "seq.h"

#include <string.h>

#include "engine.h"  // SYNTH_LEAD / SYNTH_DRUMS (constants only)

#ifdef __3DS__
#include <3ds.h>
typedef LightLock SeqLock;
#define LOCK_INIT(l) LightLock_Init(l)
#define LOCK(l) LightLock_Lock(l)
#define UNLOCK(l) LightLock_Unlock(l)
#else
#include <pthread.h>
typedef pthread_mutex_t SeqLock;
#define LOCK_INIT(l) pthread_mutex_init(l, NULL)
#define LOCK(l) pthread_mutex_lock(l)
#define UNLOCK(l) pthread_mutex_unlock(l)
#endif

#define GATE 0.55  // synth notes sound for this fraction of a step
#define MAX_EVENTS 32

typedef struct {
    int synth, note;
    float vel;  // 0 = off
} Event;

static SeqLock s_lock;
static SeqNoteFn s_out;
static SeqPattern s_pats[SEQ_SLOTS];
static int s_cur, s_queued = -1;
static bool s_song;
static float s_swing;
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

static bool slot_used(int slot) {
    const SeqPattern *p = &s_pats[slot];
    for (int s = 0; s < SEQ_STEPS; s++)
        if (p->drums[s] || p->synth[s].count) return true;
    return false;
}

// Length of the gap after step `step`: swing stretches the even steps and
// squeezes the odd ones, so each pair still adds up to two straight steps.
static double step_len_after(int step) {
    double straight = s_rate * 60.0 / s_bpm / 4.0;
    return straight * ((step & 1) == 0 ? 1.0 + s_swing : 1.0 - s_swing);
}

static int drum_note(int lane) {
    int notes[MUSIC_MAX_NOTES];
    music_key_notes(lane, PLAY_DRUM, 0, notes);
    return notes[0];
}

static int close_gate(Event *ev, int n) {
    if (!s_gate_open) return n;
    for (int i = 0; i < s_gate.count && n < MAX_EVENTS; i++) ev[n++] = (Event){SYNTH_LEAD, s_gate.notes[i], 0.0f};
    s_gate_open = false;
    return n;
}

// The bar is over: switch to the queued slot, or the next one in a song.
static void bar_end(void) {
    if (s_queued >= 0) {
        s_cur = s_queued;
        s_queued = -1;
    } else if (s_song) {
        for (int i = 1; i <= SEQ_SLOTS; i++) {
            int c = (s_cur + i) % SEQ_SLOTS;
            if (slot_used(c)) {
                s_cur = c;
                break;
            }
        }
    }
}

static int fire_step(int step, Event *ev, int n) {
    n = close_gate(ev, n);
    const SeqPattern *p = &s_pats[s_cur];
    const bool accent = (p->accents >> step) & 1u;
    uint8_t hits = p->drums[step] & (uint8_t)~s_skip_drums[step];
    s_skip_drums[step] = 0;
    for (int lane = 0; lane < SEQ_LANES; lane++)
        if ((hits & (1u << lane)) && n < MAX_EVENTS)
            ev[n++] = (Event){SYNTH_DRUMS, drum_note(lane), accent ? SEQ_VEL_DRUM_ACCENT : SEQ_VEL_DRUM};

    const SeqNote *sn = &p->synth[step];
    if (sn->count && !s_skip_synth[step]) {
        for (int i = 0; i < sn->count && n < MAX_EVENTS; i++)
            ev[n++] = (Event){SYNTH_LEAD, sn->notes[i], accent ? SEQ_VEL_SYNTH_ACCENT : SEQ_VEL_SYNTH};
        s_gate = *sn;
        s_gate_open = true;
    }
    s_skip_synth[step] = false;
    s_step = step;
    s_fired++;
    return n;
}

static void starter_locked(void) {
    memset(s_pats, 0, sizeof(s_pats));
    // A starter groove in slot A so START makes music straight away:
    // kick on the beat, snare on 2 and 4, closed hats on the off-beats.
    SeqPattern *p = &s_pats[0];
    for (int s = 0; s < SEQ_STEPS; s += 4) p->drums[s] |= 1u << 0;
    p->drums[4] |= 1u << 1;
    p->drums[12] |= 1u << 1;
    for (int s = 2; s < SEQ_STEPS; s += 4) p->drums[s] |= 1u << 2;
    p->drums[14] |= 1u << 3;
    p->accents = 1u << 0 | 1u << 8;
    s_cur = 0;
    s_queued = -1;
    s_song = false;
    s_swing = 0.0f;
}

void seq_init(SeqNoteFn out) {
    LOCK_INIT(&s_lock);
    s_out = out;
    starter_locked();
}

void seq_reset(void) {
    LOCK(&s_lock);
    starter_locked();
    UNLOCK(&s_lock);
}

void seq_advance(int frames, int rate) {
    Event ev[MAX_EVENTS];
    int n = 0;

    LOCK(&s_lock);
    s_rate = rate;
    if (s_playing) {
        if (s_start_pending) {
            s_start_pending = false;
            s_pos = 0.0;
            n = fire_step(0, ev, n);
        } else {
            s_pos += frames;
            if (s_gate_open && s_pos >= step_len_after(s_step) * GATE) n = close_gate(ev, n);
            double len;
            while (s_pos >= (len = step_len_after(s_step))) {
                s_pos -= len;
                int next = (s_step + 1) % SEQ_STEPS;
                if (next == 0) bar_end();
                n = fire_step(next, ev, n);
            }
        }
    } else {
        n = close_gate(ev, n);  // stopped: let go of anything still held
    }
    UNLOCK(&s_lock);

    // Talk to the engine outside our lock.
    if (s_out)
        for (int i = 0; i < n; i++) s_out(ev[i].synth, ev[i].note, ev[i].vel);
}

void seq_toggle_play(void) {
    LOCK(&s_lock);
    s_playing = !s_playing;
    if (s_playing) s_start_pending = true;
    s_queued = -1;
    memset(s_skip_drums, 0, sizeof(s_skip_drums));
    memset(s_skip_synth, 0, sizeof(s_skip_synth));
    UNLOCK(&s_lock);
}

void seq_set_recording(bool on) {
    LOCK(&s_lock);
    s_recording = on;
    UNLOCK(&s_lock);
}

void seq_set_bpm(int bpm) {
    if (bpm < SEQ_BPM_MIN) bpm = SEQ_BPM_MIN;
    if (bpm > SEQ_BPM_MAX) bpm = SEQ_BPM_MAX;
    LOCK(&s_lock);
    s_bpm = bpm;
    UNLOCK(&s_lock);
}

void seq_set_swing(float swing) {
    LOCK(&s_lock);
    s_swing = swing < 0.0f ? 0.0f : swing > SEQ_SWING_MAX ? SEQ_SWING_MAX : swing;
    UNLOCK(&s_lock);
}

void seq_set_song(bool on) {
    LOCK(&s_lock);
    s_song = on;
    UNLOCK(&s_lock);
}

void seq_clear(void) {
    LOCK(&s_lock);
    memset(&s_pats[s_cur], 0, sizeof(SeqPattern));
    UNLOCK(&s_lock);
}

void seq_set_pattern(const SeqPattern *p) {
    LOCK(&s_lock);
    s_pats[s_cur] = *p;
    UNLOCK(&s_lock);
}

static void select_locked(int slot) {
    if (slot < 0 || slot >= SEQ_SLOTS) return;
    if (s_playing && !s_start_pending)
        s_queued = slot == s_cur ? -1 : slot;
    else
        s_cur = slot;
}

void seq_select_slot(int slot) {
    LOCK(&s_lock);
    select_locked(slot);
    UNLOCK(&s_lock);
}

void seq_copy_to_slot(int slot) {
    if (slot < 0 || slot >= SEQ_SLOTS) return;
    LOCK(&s_lock);
    s_pats[slot] = s_pats[s_cur];
    select_locked(slot);
    UNLOCK(&s_lock);
}

void seq_set_drum(int step, int lane, bool on) {
    if (step < 0 || step >= SEQ_STEPS || lane < 0 || lane >= SEQ_LANES) return;
    LOCK(&s_lock);
    if (on)
        s_pats[s_cur].drums[step] |= (uint8_t)(1u << lane);
    else
        s_pats[s_cur].drums[step] &= (uint8_t)~(1u << lane);
    UNLOCK(&s_lock);
}

void seq_set_synth(int step, const SeqNote *note) {
    if (step < 0 || step >= SEQ_STEPS) return;
    LOCK(&s_lock);
    if (note && note->count)
        s_pats[s_cur].synth[step] = *note;
    else
        memset(&s_pats[s_cur].synth[step], 0, sizeof(SeqNote));
    UNLOCK(&s_lock);
}

void seq_set_accent(int step, bool on) {
    if (step < 0 || step >= SEQ_STEPS) return;
    LOCK(&s_lock);
    if (on)
        s_pats[s_cur].accents |= (uint16_t)(1u << step);
    else
        s_pats[s_cur].accents &= (uint16_t)~(1u << step);
    UNLOCK(&s_lock);
}

// Nearest step to "now"; *ahead is set when that is the coming step.
static int nearest_step(bool *ahead) {
    *ahead = s_pos >= step_len_after(s_step) / 2.0;
    return *ahead ? (s_step + 1) % SEQ_STEPS : s_step;
}

void seq_record_drum(int lane) {
    if (lane < 0 || lane >= SEQ_LANES) return;
    LOCK(&s_lock);
    if (s_playing && s_recording && !s_start_pending) {
        bool ahead;
        int step = nearest_step(&ahead);
        s_pats[s_cur].drums[step] |= (uint8_t)(1u << lane);
        if (ahead) s_skip_drums[step] |= (uint8_t)(1u << lane);
    }
    UNLOCK(&s_lock);
}

void seq_record_synth(const SeqNote *note) {
    if (!note || !note->count) return;
    LOCK(&s_lock);
    if (s_playing && s_recording && !s_start_pending) {
        bool ahead;
        int step = nearest_step(&ahead);
        s_pats[s_cur].synth[step] = *note;
        if (ahead) s_skip_synth[step] = true;
    }
    UNLOCK(&s_lock);
}

SeqStatus seq_status(void) {
    LOCK(&s_lock);
    SeqStatus st = {s_playing, s_recording, s_bpm, s_step, s_fired, s_cur, s_queued, s_song, s_swing, 0};
    for (int i = 0; i < SEQ_SLOTS; i++)
        if (slot_used(i)) st.used |= (uint8_t)(1u << i);
    UNLOCK(&s_lock);
    return st;
}

void seq_snapshot(SeqPattern *out) {
    LOCK(&s_lock);
    *out = s_pats[s_cur];
    UNLOCK(&s_lock);
}

void seq_get_slot(int slot, SeqPattern *out) {
    if (slot < 0 || slot >= SEQ_SLOTS) return;
    LOCK(&s_lock);
    *out = s_pats[slot];
    UNLOCK(&s_lock);
}

void seq_put_slot(int slot, const SeqPattern *p) {
    if (slot < 0 || slot >= SEQ_SLOTS) return;
    LOCK(&s_lock);
    s_pats[slot] = *p;
    UNLOCK(&s_lock);
}
