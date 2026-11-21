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

#define MAX_EVENTS 256  // events one seq_advance() can send (a step full of notes fits)
#define MAX_ACTIVE 32  // synth notes the sequencer is holding at once
#define MAX_SKIPS 16
#define MAX_REC_HELD 12

typedef struct {
    int synth, note;
    float vel;  // 0 = off
} Event;

// A synth note the sequencer started and will release at `off`, measured
// in steps since play started (fractional).
typedef struct {
    uint8_t pitch, part;
    double off;
} Active;

static SeqLock s_lock;
static SeqNoteFn s_out;
static SeqPattern s_pats[SEQ_SLOTS];
static int s_cur, s_queued = -1;
static bool s_song;
static SeqArrangement s_arr;
static int s_arr_row = -1, s_arr_rep;
static uint32_t s_ends;
static bool s_end_pending;  // the arrangement is over: stop at this bar line
static float s_swing;
static uint8_t s_muted;  // bit n = part n muted; bit PARTS = the drums

// Hits recorded slightly early (into the coming step) already sounded live;
// skip them once when that step fires so they are not heard twice.
static uint8_t s_skip_drums[SEQ_STEPS_MAX];
typedef struct {
    uint8_t step, pitch, part;
} Skip;
static Skip s_skip_notes[MAX_SKIPS];
static int s_nskip;

// Notes being recorded live, so their length can be set on release.
typedef struct {
    int index;
    uint8_t pitch, part;
    double start;
    uint8_t slot;  // the pattern it was recorded into (the slot may change before release)
} RecHeld;
static RecHeld s_rec_held[MAX_REC_HELD];
static int s_nrec;

static bool s_playing, s_recording, s_start_pending;
static int s_bpm = 112;
static int s_step;
static double s_pos;     // samples since the current step fired
static uint32_t s_ticks;  // steps fired since play started (the current step's number)
static int s_rate = 44100;
static uint32_t s_fired;
static Active s_active[MAX_ACTIVE];
static int s_nactive;

// The arpeggiator (see seq.h).
#define ARP_MAX 16
static SeqArp s_arp = {false, ARP_UP, ARP_16TH, 1};
static uint8_t s_arp_notes[ARP_MAX];  // held, sorted
static int s_arp_n, s_arp_part;
static int s_arp_idx = -1, s_arp_dir = 1;
static int s_arp_cur = -1, s_arp_cur_part;  // sounding, or -1
static double s_arp_clock;  // samples to the next free-running tick
static double s_arp_off;    // samples until the sounding note stops
static uint32_t s_arp_rng = 0x9E3779B9u;
static bool s_arp_mid_done;  // 1/32 while playing: the mid-step tick has fired

static bool slot_used(int slot) {
    const SeqPattern *p = &s_pats[slot];
    if (p->count) return true;
    for (int s = 0; s < SEQ_STEPS_MAX; s++)
        if (p->drums[s]) return true;
    return false;
}

// Length of the gap after step `step`: swing stretches the even steps and
// squeezes the odd ones, so each pair still adds up to two straight steps.
static double step_len_after(int step) {
    double straight = s_rate * 60.0 / s_bpm / 4.0;
    return straight * ((step & 1) == 0 ? 1.0 + s_swing : 1.0 - s_swing);
}

// Where the playhead is, in steps since play started.
static double now_steps(void) { return s_ticks + s_pos / step_len_after(s_step); }

static int drum_note(int lane) {
    int notes[MUSIC_MAX_NOTES];
    music_key_notes(lane, PLAY_DRUM, 0, notes);
    return notes[0];
}

// Note-offs are never dropped (a dropped one is a stuck note): note-ons only
// go out while there is still room for the off of every note sounding, so
//   n + s_nactive + (an arp note sounding) <= MAX_EVENTS
// always holds. An off moves one from the right to the left; an on that
// will need an off costs two. If a block really is that crowded, the extra
// note-ons are what's dropped.
static int owed_offs(void) { return s_nactive + (s_arp_cur >= 0); }
static bool room_for_on(int n, int cost) { return n + owed_offs() + cost <= MAX_EVENTS; }

static int release_active(int i, Event *ev, int n) {
    if (n < MAX_EVENTS) ev[n++] = (Event){part_synth(s_active[i].part), s_active[i].pitch, 0.0f};
    s_active[i] = s_active[--s_nactive];
    return n;
}

static int release_due(double now, Event *ev, int n) {
    for (int i = 0; i < s_nactive;)
        if (s_active[i].off <= now) n = release_active(i, ev, n);
        else i++;
    return n;
}

static int release_all(Event *ev, int n) {
    while (s_nactive > 0) n = release_active(s_nactive - 1, ev, n);
    return n;
}

static bool take_skip(int step, int pitch, int part) {
    for (int i = 0; i < s_nskip; i++)
        if (s_skip_notes[i].step == step && s_skip_notes[i].pitch == pitch && s_skip_notes[i].part == part) {
            s_skip_notes[i] = s_skip_notes[--s_nskip];
            return true;
        }
    return false;
}

static bool arranging(void) { return s_song && s_arr.count > 0; }

static void arr_enter(int row) {
    s_arr_row = row;
    s_arr_rep = 0;
    s_cur = s_arr.rows[row].slot;
}

// --- arpeggiator -----------------------------------------------------------

static int add_locked(SeqPattern *p, int part, int step, int pitch, int len, int vel, int flags);

static double arp_interval(void) {  // samples between ticks
    const double step = s_rate * 60.0 / s_bpm / 4.0;
    return s_arp.rate == ARP_8TH ? step * 2 : s_arp.rate == ARP_32ND ? step / 2 : step;
}

static int arp_off_event(Event *ev, int n) {
    if (s_arp_cur >= 0 && n < MAX_EVENTS) ev[n++] = (Event){part_synth(s_arp_cur_part), s_arp_cur, 0.0f};
    s_arp_cur = -1;
    return n;
}

// The next note: the held notes (over 1 or 2 octaves) walked in the mode.
static int arp_next(void) {
    const int len = s_arp_n * (s_arp.octaves >= 2 ? 2 : 1);
    if (len <= 0) return -1;
    switch (s_arp.mode) {
        case ARP_DOWN: s_arp_idx = s_arp_idx <= 0 ? len - 1 : s_arp_idx - 1; break;
        case ARP_UPDOWN:
            if (len == 1) {
                s_arp_idx = 0;
                break;
            }
            if (s_arp_idx < 0) {
                s_arp_idx = 0;
                s_arp_dir = 1;
                break;
            }
            if (s_arp_idx + s_arp_dir >= len || s_arp_idx + s_arp_dir < 0) s_arp_dir = -s_arp_dir;
            s_arp_idx += s_arp_dir;
            break;
        case ARP_RANDOM:
            s_arp_rng = s_arp_rng * 1664525u + 1013904223u;
            s_arp_idx = (int)((s_arp_rng >> 8) % (uint32_t)len);
            break;
        case ARP_UP:
        default: s_arp_idx = s_arp_idx + 1 >= len ? 0 : s_arp_idx + 1; break;
    }
    if (s_arp_idx >= len) s_arp_idx = 0;
    return s_arp_notes[s_arp_idx % s_arp_n] + 12 * (s_arp_idx / s_arp_n);
}

// One arpeggiator step. `step` is the pattern step it lands on (recorded
// there while recording), or -1 off the grid (free-running, or a 1/32 in
// between).
static int arp_tick(int step, Event *ev, int n) {
    n = arp_off_event(ev, n);
    const int pitch = arp_next();
    if (pitch < 0 || pitch > 127) return n;
    const float vel = 0.75f;
    if (!room_for_on(n, 2)) return n;
    ev[n++] = (Event){part_synth(s_arp_part), pitch, vel};
    s_arp_cur = pitch;
    s_arp_cur_part = s_arp_part;
    s_arp_off = arp_interval() * 0.6;  // a little gap between notes
    if (step >= 0 && s_playing && s_recording && !((s_muted >> s_arp_part) & 1u)) {
        SeqPattern *p = &s_pats[s_cur];
        add_locked(p, s_arp_part, step, pitch, s_arp.rate == ARP_8TH ? 2 : 1, (int)(vel * 255), 0);
    }
    return n;
}

static bool arp_active(void) { return s_arp.on && s_arp_n > 0; }

// The bar is over: switch to the queued slot, or the next one in a song.
static void bar_end(void) {
    if (arranging()) {
        if (s_arr_row < 0 || s_arr_row >= s_arr.count) {
            arr_enter(0);
        } else if (++s_arr_rep >= s_arr.rows[s_arr_row].reps) {
            if (s_arr_row + 1 < s_arr.count)
                arr_enter(s_arr_row + 1);
            else if (s_arr.loop)
                arr_enter(0);
            else
                s_end_pending = true;
        }
        s_queued = -1;
        return;
    }
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

static int start_note(int part, int pitch, float vel, double off, Event *ev, int n) {
    // Retriggering a held pitch on the same part: let the old one go first,
    // so its release can't cut the new note short.
    for (int i = 0; i < s_nactive; i++)
        if (s_active[i].pitch == pitch && s_active[i].part == part) {
            n = release_active(i, ev, n);
            break;
        }
    if (s_nactive == MAX_ACTIVE) n = release_active(0, ev, n);  // full: drop the oldest
    if (!room_for_on(n, 2)) return n;  // (its off is owed from now on)
    s_active[s_nactive++] = (Active){(uint8_t)pitch, (uint8_t)part, off};
    ev[n++] = (Event){part_synth(part), pitch, vel};
    return n;
}

static int fire_step(int step, Event *ev, int n) {
    const SeqPattern *p = &s_pats[s_cur];
    const bool accent = (p->accents >> step) & 1ull;
    uint8_t hits = p->drums[step] & (uint8_t)~s_skip_drums[step];
    s_skip_drums[step] = 0;
    // An arrangement row can drop parts out on top of the mixer's mutes.
    const uint8_t muted = s_muted | (arranging() && s_arr_row >= 0 ? s_arr.rows[s_arr_row].mutes : 0);
    if ((muted >> PARTS) & 1u) hits = 0;
    for (int lane = 0; lane < SEQ_LANES; lane++)
        if ((hits & (1u << lane)) && room_for_on(n, 1))  // (drums are one-shots: no off)
            ev[n++] = (Event){SYNTH_DRUMS, drum_note(lane), accent ? SEQ_VEL_DRUM_ACCENT : SEQ_VEL_DRUM};

    for (int i = 0; i < p->count; i++) {
        const SeqRollNote *rn = &p->notes[i];
        if (rn->step != step || take_skip(step, rn->pitch, rn->part)) continue;
        if ((muted >> rn->part) & 1u) continue;
        float vel = rn->vel / 255.0f;
        if (accent && vel < SEQ_VEL_SYNTH_ACCENT) vel = SEQ_VEL_SYNTH_ACCENT;
        // A slide holds on past the start of the next step, so the next note
        // arrives while this one still sounds (legato).
        const double gate = (rn->flags & SEQ_NOTE_SLIDE) ? 1.15 : SEQ_GATE;
        n = start_note(rn->part, rn->pitch, vel, s_ticks + (rn->len - 1) + gate, ev, n);
    }
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
    p->accents = 1ull << 0 | 1ull << 8;
    for (int i = 0; i < SEQ_SLOTS; i++) s_pats[i].len = SEQ_STEPS;
    s_cur = 0;
    s_queued = -1;
    s_song = false;
    s_swing = 0.0f;
    memset(&s_arr, 0, sizeof(s_arr));
    s_arr_row = -1;
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
            s_ticks = 0;
            if (arranging()) arr_enter(0);  // a song plays from its first row
            n = fire_step(0, ev, n);
            s_arp_mid_done = false;
            if (arp_active()) n = arp_tick(0, ev, n);
        } else {
            s_pos += frames;
            double len;
            while (s_pos >= (len = step_len_after(s_step))) {
                s_pos -= len;
                s_ticks++;
                n = release_due(s_ticks, ev, n);  // notes ending on this boundary go first
                int next = (s_step + 1) % seq_len(&s_pats[s_cur]);
                if (next == 0) bar_end();
                if (s_end_pending) {  // the arrangement played to its end
                    s_end_pending = false;
                    s_playing = false;
                    s_ends++;
                    n = release_all(ev, n);
                    break;
                }
                n = fire_step(next, ev, n);
                s_arp_mid_done = false;
                // The arpeggiator, locked to the steps: every 2nd for 1/8.
                if (arp_active() && (s_arp.rate != ARP_8TH || (s_ticks & 1) == 0)) n = arp_tick(next, ev, n);
            }
            if (arp_active() && s_arp.rate == ARP_32ND && !s_arp_mid_done && s_pos >= step_len_after(s_step) / 2.0) {
                s_arp_mid_done = true;  // 1/32: one more between the steps
                n = arp_tick(-1, ev, n);
            }
            n = release_due(now_steps(), ev, n);
        }
    } else {
        n = release_all(ev, n);  // stopped: let go of anything still held
        if (arp_active()) {  // free-running at the tempo
            s_arp_clock -= frames;
            if (s_arp_clock <= 0) {
                n = arp_tick(-1, ev, n);
                s_arp_clock += arp_interval();
                if (s_arp_clock < 0) s_arp_clock = arp_interval();
            }
        }
    }
    // The sounding arp note ends after its gate.
    if (s_arp_cur >= 0) {
        s_arp_off -= frames;
        if (s_arp_off <= 0) n = arp_off_event(ev, n);
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
    s_nskip = 0;
    s_nrec = 0;
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
    s_arr_row = -1;
    UNLOCK(&s_lock);
}

void seq_set_arrangement(const SeqArrangement *a) {
    LOCK(&s_lock);
    s_arr = *a;
    if (s_arr.count > SEQ_ARR_ROWS) s_arr.count = SEQ_ARR_ROWS;
    for (int i = 0; i < s_arr.count; i++) {
        SeqArrRow *r = &s_arr.rows[i];
        if (r->slot >= SEQ_SLOTS) r->slot = 0;
        if (r->reps < 1) r->reps = 1;
        if (r->reps > SEQ_ARR_REPS_MAX) r->reps = SEQ_ARR_REPS_MAX;
    }
    if (s_arr_row >= s_arr.count) s_arr_row = s_arr.count - 1;  // edited under the playhead
    UNLOCK(&s_lock);
}

void seq_get_arrangement(SeqArrangement *out) {
    LOCK(&s_lock);
    *out = s_arr;
    UNLOCK(&s_lock);
}

void seq_set_part_mute(int part, bool muted) {
    if (part < 0 || part > PARTS) return;  // PARTS = the drums
    LOCK(&s_lock);
    if (muted)
        s_muted |= (uint8_t)(1u << part);
    else
        s_muted &= (uint8_t)~(1u << part);
    UNLOCK(&s_lock);
}

// A pattern's length is always explicit in here (16, 32 or 64).
static void normalise_len(SeqPattern *p) {
    if (p->len != 32 && p->len != 64) p->len = SEQ_STEPS;
}

void seq_clear(void) {  // everything but the length
    LOCK(&s_lock);
    const uint8_t len = s_pats[s_cur].len;
    memset(&s_pats[s_cur], 0, sizeof(SeqPattern));
    s_pats[s_cur].len = len;
    normalise_len(&s_pats[s_cur]);
    s_nrec = 0;
    UNLOCK(&s_lock);
}

void seq_set_arp(const SeqArp *a) {
    LOCK(&s_lock);
    s_arp = *a;
    if (s_arp.mode >= ARP_MODES) s_arp.mode = ARP_UP;
    if (s_arp.rate >= ARP_RATES) s_arp.rate = ARP_16TH;
    if (s_arp.octaves < 1 || s_arp.octaves > 2) s_arp.octaves = 1;
    UNLOCK(&s_lock);
}

void seq_arp_hold(int part, const int *pitches, int n) {
    if (n > ARP_MAX) n = ARP_MAX;
    LOCK(&s_lock);
    const bool was = s_arp_n > 0;
    // Sorted, no repeats.
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (pitches[i] < 0 || pitches[i] > 127) continue;
        int j = c;
        bool dup = false;
        for (int k = 0; k < c; k++) dup |= s_arp_notes[k] == pitches[i];
        if (dup) continue;
        while (j > 0 && s_arp_notes[j - 1] > pitches[i]) {
            s_arp_notes[j] = s_arp_notes[j - 1];
            j--;
        }
        s_arp_notes[j] = (uint8_t)pitches[i];
        c++;
    }
    s_arp_n = c;
    s_arp_part = part;
    if (c == 0) {
        s_arp_idx = -1;  // the next hold starts from the beginning
        s_arp_dir = 1;
    } else if (!was) {
        s_arp_clock = 0;  // stopped: the first note comes straight away
    }
    UNLOCK(&s_lock);
}

void seq_set_pattern(const SeqPattern *p) {
    LOCK(&s_lock);
    s_pats[s_cur] = *p;
    normalise_len(&s_pats[s_cur]);
    s_nrec = 0;
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
    if (step < 0 || step >= SEQ_STEPS_MAX || lane < 0 || lane >= SEQ_LANES) return;
    LOCK(&s_lock);
    if (on)
        s_pats[s_cur].drums[step] |= (uint8_t)(1u << lane);
    else
        s_pats[s_cur].drums[step] &= (uint8_t)~(1u << lane);
    UNLOCK(&s_lock);
}

void seq_set_accent(int step, bool on) {
    if (step < 0 || step >= SEQ_STEPS_MAX) return;
    LOCK(&s_lock);
    if (on)
        s_pats[s_cur].accents |= 1ull << step;
    else
        s_pats[s_cur].accents &= ~(1ull << step);
    UNLOCK(&s_lock);
}

// --- piano roll (callers hold the lock) ------------------------------------

static void remove_locked(SeqPattern *p, int index) {
    if (index < 0 || index >= p->count) return;
    p->notes[index] = p->notes[--p->count];
    s_nrec = 0;  // indices moved: stop tracking held recordings
}

// Every copy of a note goes through here with its flags (SLIDE...), so
// growing a pattern or replacing a part's notes keeps them.
static int add_locked(SeqPattern *p, int part, int step, int pitch, int len, int vel, int flags) {
    if (p->count >= SEQ_NOTES || step < 0 || step >= SEQ_STEPS_MAX || part < 0 || part >= PARTS) return -1;
    if (pitch < 0) pitch = 0;
    if (pitch > 127) pitch = 127;
    len = len < 1 ? 1 : len > SEQ_STEPS_MAX ? SEQ_STEPS_MAX : len;
    vel = vel < 1 ? 1 : vel > 255 ? 255 : vel;
    // One note per (part, step, pitch): adding again replaces it.
    for (int i = 0; i < p->count; i++)
        if (p->notes[i].step == step && p->notes[i].pitch == pitch && p->notes[i].part == part) {
            p->notes[i].len = (uint8_t)len;
            p->notes[i].vel = (uint8_t)vel;
            p->notes[i].flags = (uint8_t)flags;
            return i;
        }
    p->notes[p->count] = (SeqRollNote){(uint8_t)step, (uint8_t)pitch, (uint8_t)len, (uint8_t)vel, (uint8_t)part, (uint8_t)flags};
    return p->count++;
}

void seq_set_bars(int bars) {
    const int len = (bars >= 4 ? 4 : bars >= 2 ? 2 : 1) * SEQ_STEPS;
    LOCK(&s_lock);
    SeqPattern *p = &s_pats[s_cur];
    const int old = seq_len(p);
    if (len > old) {  // the new bars start as copies of what's there
        for (int s = old; s < len; s++) {
            p->drums[s] = p->drums[s % old];
            p->fx[s] = p->fx[s % old];
            p->fx_x[s] = p->fx_x[s % old];
            p->fx_y[s] = p->fx_y[s % old];
            if ((p->accents >> (s % old)) & 1ull) p->accents |= 1ull << s;
            else p->accents &= ~(1ull << s);
        }
        // Notes already living in the new bars (from before a shrink) go,
        // then the copies come in.
        for (int i = 0; i < p->count;)
            if (p->notes[i].step >= old) remove_locked(p, i);
            else i++;
        const int n = p->count;
        for (int start = old; start < len; start += old)
            for (int i = 0; i < n; i++) {
                const SeqRollNote *rn = &p->notes[i];
                add_locked(p, rn->part, rn->step + start, rn->pitch, rn->len, rn->vel, rn->flags);
            }
    }
    p->len = (uint8_t)len;
    if (s_step >= len) s_step = len - 1;
    UNLOCK(&s_lock);
}

static void clear_step_locked(SeqPattern *p, int part, int step) {
    for (int i = 0; i < p->count;)
        if (p->notes[i].step == step && p->notes[i].part == part) remove_locked(p, i);
        else i++;
}

void seq_set_synth(int part, int step, const SeqNote *note) {
    if (step < 0 || step >= SEQ_STEPS_MAX) return;
    LOCK(&s_lock);
    SeqPattern *p = &s_pats[s_cur];
    clear_step_locked(p, part, step);
    if (note)
        for (int i = 0; i < note->count; i++) add_locked(p, part, step, note->notes[i], 1, (int)(SEQ_VEL_SYNTH * 255), 0);
    UNLOCK(&s_lock);
}

int seq_roll_add(int part, int step, int pitch, int len, int vel) {
    LOCK(&s_lock);
    int i = add_locked(&s_pats[s_cur], part, step, pitch, len, vel, 0);
    UNLOCK(&s_lock);
    return i;
}

void seq_set_part_pattern(int part, const SeqPattern *src) {
    LOCK(&s_lock);
    SeqPattern *p = &s_pats[s_cur];
    for (int i = 0; i < p->count;)
        if (p->notes[i].part == part) remove_locked(p, i);
        else i++;
    for (int i = 0; i < src->count; i++) {
        const SeqRollNote *n = &src->notes[i];
        add_locked(p, part, n->step, n->pitch, n->len, n->vel, n->flags);
    }
    memcpy(p->drums, src->drums, sizeof(p->drums));
    p->accents = src->accents;
    UNLOCK(&s_lock);
}

void seq_roll_set(int index, int step, int pitch, int len) {
    LOCK(&s_lock);
    SeqPattern *p = &s_pats[s_cur];
    if (index >= 0 && index < p->count) {
        SeqRollNote *rn = &p->notes[index];
        const int last = seq_len(p) - 1;
        rn->step = (uint8_t)(step < 0 ? 0 : step > last ? last : step);
        rn->pitch = (uint8_t)(pitch < 0 ? 0 : pitch > 127 ? 127 : pitch);
        rn->len = (uint8_t)(len < 1 ? 1 : len > SEQ_STEPS_MAX ? SEQ_STEPS_MAX : len);
    }
    UNLOCK(&s_lock);
}

void seq_set_fx(int step, int kind1, float x, float y) {
    if (step < 0 || step >= SEQ_STEPS_MAX) return;
    LOCK(&s_lock);
    SeqPattern *p = &s_pats[s_cur];
    p->fx[step] = (uint8_t)(kind1 < 0 ? 0 : kind1);
    p->fx_x[step] = (uint8_t)(x <= 0.0f ? 0 : x >= 1.0f ? 255 : x * 255.0f + 0.5f);
    p->fx_y[step] = (uint8_t)(y <= 0.0f ? 0 : y >= 1.0f ? 255 : y * 255.0f + 0.5f);
    UNLOCK(&s_lock);
}

void seq_roll_set_flags(int index, int flags) {
    LOCK(&s_lock);
    SeqPattern *p = &s_pats[s_cur];
    if (index >= 0 && index < p->count) p->notes[index].flags = (uint8_t)flags;
    UNLOCK(&s_lock);
}

void seq_roll_set_vel(int index, int vel) {
    LOCK(&s_lock);
    SeqPattern *p = &s_pats[s_cur];
    if (index >= 0 && index < p->count) p->notes[index].vel = (uint8_t)(vel < 1 ? 1 : vel > 255 ? 255 : vel);
    UNLOCK(&s_lock);
}

void seq_roll_remove(int index) {
    LOCK(&s_lock);
    remove_locked(&s_pats[s_cur], index);
    UNLOCK(&s_lock);
}

int seq_roll_find(int part, int step, int pitch) {
    LOCK(&s_lock);
    const SeqPattern *p = &s_pats[s_cur];
    int found = -1;
    for (int i = 0; i < p->count && found < 0; i++) {
        const SeqRollNote *rn = &p->notes[i];
        if (rn->part == part && rn->pitch == pitch && step >= rn->step && step < rn->step + rn->len) found = i;
    }
    UNLOCK(&s_lock);
    return found;
}

int seq_notes_at(const SeqPattern *p, int part, int step, int *pitches, int max) {
    int n = 0;
    for (int i = 0; i < p->count; i++)
        if (p->notes[i].step == step && (part < 0 || p->notes[i].part == part)) {
            if (n < max) pitches[n] = p->notes[i].pitch;
            n++;
        }
    return n;
}

// --- live recording --------------------------------------------------------

// Nearest step to "now"; *ahead is set when that is the coming step.
static int nearest_step(bool *ahead) {
    *ahead = s_pos >= step_len_after(s_step) / 2.0;
    return *ahead ? (s_step + 1) % seq_len(&s_pats[s_cur]) : s_step;
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

void seq_record_synth(int part, const SeqNote *note) {
    if (!note || !note->count) return;
    LOCK(&s_lock);
    if (s_playing && s_recording && !s_start_pending) {
        bool ahead;
        int step = nearest_step(&ahead);
        SeqPattern *p = &s_pats[s_cur];
        for (int i = 0; i < note->count; i++) {
            int idx = add_locked(p, part, step, note->notes[i], 1, (int)(SEQ_VEL_SYNTH * 255), 0);
            if (idx < 0) continue;
            if (ahead && s_nskip < MAX_SKIPS) s_skip_notes[s_nskip++] = (Skip){(uint8_t)step, note->notes[i], (uint8_t)part};
            if (s_nrec < MAX_REC_HELD)  // remember when it started, to set its length on release
                s_rec_held[s_nrec++] = (RecHeld){idx, note->notes[i], (uint8_t)part, s_ticks + (ahead ? 1.0 : 0.0), (uint8_t)s_cur};
        }
    }
    UNLOCK(&s_lock);
}

void seq_record_release(int part, const SeqNote *note) {
    if (!note || !note->count) return;
    LOCK(&s_lock);
    const double now = now_steps();
    for (int k = 0; k < note->count; k++)
        for (int i = 0; i < s_nrec; i++) {
            if (s_rec_held[i].pitch != note->notes[k] || s_rec_held[i].part != part) continue;
            SeqPattern *p = &s_pats[s_rec_held[i].slot];
            int idx = s_rec_held[i].index;
            if (idx < p->count && p->notes[idx].pitch == s_rec_held[i].pitch) {
                int len = (int)(now - s_rec_held[i].start + 0.5);
                p->notes[idx].len = (uint8_t)(len < 1 ? 1 : len > SEQ_STEPS_MAX ? SEQ_STEPS_MAX : len);
            }
            s_rec_held[i] = s_rec_held[--s_nrec];
            break;
        }
    UNLOCK(&s_lock);
}

SeqStatus seq_status(void) {
    LOCK(&s_lock);
    SeqStatus st = {s_playing, s_recording, s_bpm, s_step, s_fired, s_cur, s_queued, s_song, s_swing, 0, s_muted,
                    arranging() && s_playing ? s_arr_row : -1, s_arr_rep, s_ends, {0}};
    for (int i = 0; i < SEQ_SLOTS; i++) {
        if (slot_used(i)) st.used |= (uint8_t)(1u << i);
        st.slot_bars[i] = (uint8_t)seq_bars(&s_pats[i]);
    }
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
    normalise_len(&s_pats[slot]);
    UNLOCK(&s_lock);
}
