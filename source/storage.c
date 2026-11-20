// storage.c -- see storage.h.
//
// Format: "VIBE", u16 version, u16 payload length, then fields written one
// by one (little-endian, as the 3DS and every build machine are), then a
// simple checksum. Writing field by field keeps struct padding out of it.

#include "storage.h"

#include <stdio.h>
#include <string.h>

#define VERSION 15 // 2: slots, accents, song, swing; 3: name; 4: unsaved; 5: changed audio; 6: roll; 7: parts; 8: mixer;
                   // 9: tape sources; 10: sample edit; 11: arrangement, tape mutes;
                   // 12: FX automation; 13: patterns of 1-4 bars, 128 notes; 14: key, arp;
                   // 15: mixer ECHO / VERB sends
#define MAX_PAYLOAD 12288

typedef struct {
    uint8_t buf[MAX_PAYLOAD];
    size_t n;
    bool ok;
} Writer;

static void put(Writer *w, const void *p, size_t len) {
    if (w->n + len > MAX_PAYLOAD) {
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->n, p, len);
    w->n += len;
}

typedef struct {
    const uint8_t *buf;
    size_t n, pos;
    bool ok;
} Reader;

static void get(Reader *r, void *p, size_t len) {
    if (r->pos + len > r->n) {
        r->ok = false;
        memset(p, 0, len);
        return;
    }
    memcpy(p, r->buf + r->pos, len);
    r->pos += len;
}

static uint32_t checksum(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;  // FNV-1a
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void put_pattern(Writer *w, const SeqPattern *p) {
    const uint8_t len = (uint8_t)seq_len(p);
    put(w, &len, 1);
    put(w, p->drums, SEQ_STEPS_MAX);
    put(w, &p->accents, 8);
    put(w, &p->count, 1);
    for (int i = 0; i < p->count; i++) {
        const SeqRollNote *n = &p->notes[i];
        uint8_t b[5] = {n->step, n->pitch, n->len, n->vel, n->part};
        put(w, b, 5);
    }
}

// version 13: length, 64 steps of drums, 64 accent bits, then the notes.
// versions 6-12: one bar: 16 steps of drums, 16 accent bits, the notes.
// versions 1-5: drums, one note-or-chord per step, then (2+) accents; each
// becomes one-step-long roll notes.
static void get_pattern(Reader *r, SeqPattern *p, int version) {
    memset(p, 0, sizeof(*p));
    p->len = SEQ_STEPS;
    if (version >= 13) {
        get(r, &p->len, 1);
        if (p->len != 16 && p->len != 32 && p->len != 64) r->ok = false;
        get(r, p->drums, SEQ_STEPS_MAX);
        get(r, &p->accents, 8);
    } else {
        get(r, p->drums, SEQ_STEPS);
    }
    if (version >= 6) {
        if (version < 13) {
            uint16_t acc;
            get(r, &acc, 2);
            p->accents = acc;
        }
        get(r, &p->count, 1);
        if (p->count > SEQ_NOTES || (version < 13 && p->count > 64)) {
            r->ok = false;
            return;
        }
        const int bytes = version >= 7 ? 5 : 4;  // 7 adds the part
        const int len = seq_len(p);
        for (int i = 0; i < p->count; i++) {
            uint8_t b[5] = {0, 0, 0, 0, 0};
            get(r, b, (size_t)bytes);
            p->notes[i] = (SeqRollNote){b[0], b[1], b[2], b[3], b[4]};
            if (b[0] >= len || b[2] < 1 || b[2] > SEQ_STEPS_MAX || b[1] > 127 || b[4] >= PARTS) r->ok = false;
        }
        return;
    }
    for (int s = 0; s < SEQ_STEPS; s++) {
        uint8_t key, count, notes[MUSIC_MAX_NOTES];
        get(r, &key, 1);
        get(r, &count, 1);
        get(r, notes, MUSIC_MAX_NOTES);
        if (count > MUSIC_MAX_NOTES) r->ok = false;
        for (int i = 0; i < count && i < MUSIC_MAX_NOTES && p->count < SEQ_NOTES; i++)
            p->notes[p->count++] = (SeqRollNote){(uint8_t)s, notes[i], 1, (uint8_t)(SEQ_VEL_SYNTH * 255), 0};
    }
    if (version >= 2) {
        uint16_t acc;
        get(r, &acc, 2);
        p->accents = acc;
    }
}

static void encode(Writer *w, const SaveState *st) {
    // Part 1 sits where older versions kept their single sound.
    int32_t ints[] = {st->theme, st->parts[0].sound, st->parts[0].patch, st->octave, st->bpm};
    put(w, ints, sizeof(ints));
    put(w, st->parts[0].macro, sizeof(st->parts[0].macro));
    put(w, st->params, sizeof(st->params));
    put(w, st->fx_x, sizeof(st->fx_x));
    put(w, st->fx_y, sizeof(st->fx_y));
    int32_t slot = st->slot;
    uint8_t song = st->song;
    put(w, &slot, sizeof(slot));
    put(w, &song, 1);
    put(w, &st->swing, sizeof(st->swing));
    for (int i = 0; i < SEQ_SLOTS; i++) put_pattern(w, &st->slots[i]);
    put(w, st->name, sizeof(st->name));
    uint8_t unsaved = st->unsaved, mic = st->mic_changed;
    put(w, &unsaved, 1);
    put(w, &st->tape_changed, 1);
    put(w, &mic, 1);
    uint8_t part = (uint8_t)st->part;
    put(w, &part, 1);
    put(w, &st->muted, 1);
    for (int k = 1; k < PARTS; k++) {
        int32_t sp[2] = {st->parts[k].sound, st->parts[k].patch};
        put(w, sp, sizeof(sp));
        put(w, st->parts[k].macro, sizeof(st->parts[k].macro));
    }
    for (int k = 0; k < MIX_STRIPS; k++) {  // version 8
        put(w, &st->mix[k].level, sizeof(float));
        put(w, st->mix[k].knob, sizeof(float) * MIX_KNOBS);
    }
    put(w, &st->solo, 1);
    put(w, st->tape_source, sizeof(st->tape_source));  // version 9
    put(w, &st->sample_edit.start, sizeof(float));      // version 10
    put(w, &st->sample_edit.end, sizeof(float));
    put(w, &st->sample_edit.slices, 1);
    uint8_t loop = st->arr.loop;  // version 11
    put(w, &st->arr.count, 1);
    put(w, &loop, 1);
    for (int i = 0; i < SEQ_ARR_ROWS; i++) {
        const uint8_t row[3] = {st->arr.rows[i].slot, st->arr.rows[i].reps, st->arr.rows[i].mutes};
        put(w, row, 3);
    }
    put(w, &st->tape_muted, 1);
    for (int i = 0; i < SEQ_SLOTS; i++) {  // version 12 (13: all 64 steps)
        put(w, st->slots[i].fx, SEQ_STEPS_MAX);
        put(w, st->slots[i].fx_x, SEQ_STEPS_MAX);
        put(w, st->slots[i].fx_y, SEQ_STEPS_MAX);
    }
    const uint8_t extra[6] = {st->key_root, st->key_scale, st->arp.on, st->arp.mode, st->arp.rate, st->arp.octaves};
    put(w, extra, sizeof(extra));  // version 14
}

// version 1 files hold one pattern (no accents); it becomes slot A.
static void decode(Reader *r, SaveState *st, int version) {
    int32_t ints[5];
    get(r, ints, sizeof(ints));
    st->theme = ints[0];
    st->parts[0].sound = ints[1];
    st->parts[0].patch = ints[2];
    st->octave = ints[3];
    st->bpm = ints[4];
    get(r, st->parts[0].macro, sizeof(st->parts[0].macro));
    for (int k = 1; k < PARTS; k++) st->parts[k].sound = -1;  // older files: defaults
    st->part = 0;
    st->muted = 0;
    for (int k = 0; k < MIX_STRIPS; k++) mixer_strip_default(&st->mix[k]);
    st->solo = 0;
    for (int t = 0; t < TAPE_TRACKS; t++) st->tape_source[t] = TAPE_SRC_ALL;
    sampler_edit_default(&st->sample_edit);
    st->old_mic_macros = version < 10;
    get(r, st->params, sizeof(st->params));
    get(r, st->fx_x, sizeof(st->fx_x));
    get(r, st->fx_y, sizeof(st->fx_y));
    memset(st->slots, 0, sizeof(st->slots));
    st->slot = 0;
    st->song = false;
    st->swing = 0.0f;
    if (version == 1) {
        get_pattern(r, &st->slots[0], 1);
        return;
    }
    int32_t slot;
    uint8_t song;
    get(r, &slot, sizeof(slot));
    get(r, &song, 1);
    get(r, &st->swing, sizeof(st->swing));
    st->slot = slot >= 0 && slot < SEQ_SLOTS ? slot : 0;
    st->song = song != 0;
    for (int i = 0; i < SEQ_SLOTS; i++) get_pattern(r, &st->slots[i], version);
    if (version >= 3) {
        get(r, st->name, sizeof(st->name));
        st->name[sizeof(st->name) - 1] = '\0';
    }
    if (version >= 4) {
        uint8_t unsaved;
        get(r, &unsaved, 1);
        st->unsaved = unsaved != 0;
    }
    if (version >= 5) {
        uint8_t mic;
        get(r, &st->tape_changed, 1);
        get(r, &mic, 1);
        st->mic_changed = mic != 0;
    }
    if (version >= 7) {
        uint8_t part;
        get(r, &part, 1);
        get(r, &st->muted, 1);
        st->part = part < PARTS ? part : 0;
        for (int k = 1; k < PARTS; k++) {
            int32_t sp[2];
            get(r, sp, sizeof(sp));
            st->parts[k].sound = sp[0];
            st->parts[k].patch = sp[1];
            get(r, st->parts[k].macro, sizeof(st->parts[k].macro));
        }
    }
    if (version >= 8) {
        for (int k = 0; k < MIX_STRIPS; k++) {
            get(r, &st->mix[k].level, sizeof(float));
            // Version 15 added the ECHO / VERB sends (the last two knobs);
            // older songs keep the full sends they effectively had.
            const int knobs = version >= 15 ? MIX_KNOBS : MIX_ECHO;
            get(r, st->mix[k].knob, sizeof(float) * (size_t)knobs);
            if (!(st->mix[k].level >= 0.0f && st->mix[k].level <= 1.0f)) r->ok = false;  // NaN fails too
            for (int i = 0; i < MIX_KNOBS; i++)
                if (!(st->mix[k].knob[i] >= 0.0f && st->mix[k].knob[i] <= 1.0f)) r->ok = false;
        }
        get(r, &st->solo, 1);
        st->solo &= (uint8_t)((1u << MIX_STRIPS) - 1);
    }
    if (version >= 9) {
        get(r, st->tape_source, sizeof(st->tape_source));
        for (int t = 0; t < TAPE_TRACKS; t++)
            if (st->tape_source[t] < TAPE_SRC_ALL || st->tape_source[t] >= TAPE_SOURCES) r->ok = false;
    }
    if (version >= 10) {
        get(r, &st->sample_edit.start, sizeof(float));
        get(r, &st->sample_edit.end, sizeof(float));
        get(r, &st->sample_edit.slices, 1);
        SampleEdit e = st->sample_edit;
        sampler_edit_clamp(&e);
        if (memcmp(&e, &st->sample_edit, sizeof(e)) != 0) r->ok = false;  // out of range: damaged
    }
    if (version >= 11) {
        uint8_t loop;
        get(r, &st->arr.count, 1);
        get(r, &loop, 1);
        st->arr.loop = loop != 0;
        if (st->arr.count > SEQ_ARR_ROWS) r->ok = false;
        for (int i = 0; i < SEQ_ARR_ROWS; i++) {
            uint8_t row[3];
            get(r, row, 3);
            st->arr.rows[i] = (SeqArrRow){row[0], row[1], row[2]};
            if (i < st->arr.count && (row[0] >= SEQ_SLOTS || row[1] < 1 || row[1] > SEQ_ARR_REPS_MAX)) r->ok = false;
        }
        get(r, &st->tape_muted, 1);
    }
    if (version >= 12) {
        const int n = version >= 13 ? SEQ_STEPS_MAX : SEQ_STEPS;
        for (int i = 0; i < SEQ_SLOTS; i++) {
            get(r, st->slots[i].fx, (size_t)n);
            get(r, st->slots[i].fx_x, (size_t)n);
            get(r, st->slots[i].fx_y, (size_t)n);
            for (int s = 0; s < n; s++)  // (FX kinds)
                if (st->slots[i].fx[s] > 4) r->ok = false;  // FX_COUNT kinds
        }
    }
    st->arp = (SeqArp){false, ARP_UP, ARP_16TH, 1};
    if (version >= 14) {
        uint8_t extra[6];
        get(r, extra, sizeof(extra));
        st->key_root = extra[0];
        st->key_scale = extra[1];
        st->arp = (SeqArp){extra[2] != 0, extra[3], extra[4], extra[5]};
        if (extra[0] > 11 || extra[1] >= MUSIC_SCALES || extra[3] >= ARP_MODES || extra[4] >= ARP_RATES || extra[5] < 1 ||
            extra[5] > 2)
            r->ok = false;
    }
}

bool storage_save(const char *path, const SaveState *st) {
    static Writer w;  // big: not on the stack
    w.n = 0;
    w.ok = true;
    encode(&w, st);
    if (!w.ok) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint16_t head[2] = {VERSION, (uint16_t)w.n};
    uint32_t sum = checksum(w.buf, w.n);
    bool ok = fwrite("VIBE", 1, 4, f) == 4 && fwrite(head, sizeof(head), 1, f) == 1 && fwrite(w.buf, 1, w.n, f) == w.n &&
              fwrite(&sum, sizeof(sum), 1, f) == 1;
    return fclose(f) == 0 && ok;
}

bool storage_load(const char *path, SaveState *st) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[4];
    uint16_t head[2];
    static uint8_t buf[MAX_PAYLOAD];  // big: not on the stack
    uint32_t sum = 0;
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "VIBE", 4) == 0 && fread(head, sizeof(head), 1, f) == 1 &&
              (head[0] >= 1 && head[0] <= VERSION) && head[1] <= MAX_PAYLOAD && fread(buf, 1, head[1], f) == head[1] &&
              fread(&sum, sizeof(sum), 1, f) == 1 && sum == checksum(buf, head[1]);
    fclose(f);
    if (!ok) return false;
    static SaveState tmp;
    Reader r = {buf, head[1], 0, true};
    memset(&tmp, 0, sizeof(tmp));
    decode(&r, &tmp, head[0]);
    if (!r.ok) return false;
    *st = tmp;
    return true;
}
