// storage.c -- see storage.h.
//
// Format: "VIBE", u16 version, u16 payload length, then fields written one
// by one (little-endian, as the 3DS and every build machine are), then a
// simple checksum. Writing field by field keeps struct padding out of it.

#include "storage.h"

#include <stdio.h>
#include <string.h>

#define VERSION 5  // 2: slots, accents, song, swing; 3: song name; 4: unsaved; 5: changed audio
#define MAX_PAYLOAD 2048

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
    put(w, p->drums, sizeof(p->drums));
    for (int s = 0; s < SEQ_STEPS; s++) {
        put(w, &p->synth[s].key, 1);
        put(w, &p->synth[s].count, 1);
        put(w, p->synth[s].notes, MUSIC_MAX_NOTES);
    }
    put(w, &p->accents, sizeof(p->accents));
}

static void get_pattern(Reader *r, SeqPattern *p, bool accents) {
    get(r, p->drums, sizeof(p->drums));
    for (int s = 0; s < SEQ_STEPS; s++) {
        get(r, &p->synth[s].key, 1);
        get(r, &p->synth[s].count, 1);
        get(r, p->synth[s].notes, MUSIC_MAX_NOTES);
        if (p->synth[s].count > MUSIC_MAX_NOTES) r->ok = false;
    }
    if (accents) get(r, &p->accents, sizeof(p->accents));
}

static void encode(Writer *w, const SaveState *st) {
    int32_t ints[] = {st->theme, st->sound, st->patch, st->octave, st->bpm};
    put(w, ints, sizeof(ints));
    put(w, st->macro, sizeof(st->macro));
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
}

// version 1 files hold one pattern (no accents); it becomes slot A.
static void decode(Reader *r, SaveState *st, int version) {
    int32_t ints[5];
    get(r, ints, sizeof(ints));
    st->theme = ints[0];
    st->sound = ints[1];
    st->patch = ints[2];
    st->octave = ints[3];
    st->bpm = ints[4];
    get(r, st->macro, sizeof(st->macro));
    get(r, st->params, sizeof(st->params));
    get(r, st->fx_x, sizeof(st->fx_x));
    get(r, st->fx_y, sizeof(st->fx_y));
    memset(st->slots, 0, sizeof(st->slots));
    st->slot = 0;
    st->song = false;
    st->swing = 0.0f;
    if (version == 1) {
        get_pattern(r, &st->slots[0], false);
        return;
    }
    int32_t slot;
    uint8_t song;
    get(r, &slot, sizeof(slot));
    get(r, &song, 1);
    get(r, &st->swing, sizeof(st->swing));
    st->slot = slot >= 0 && slot < SEQ_SLOTS ? slot : 0;
    st->song = song != 0;
    for (int i = 0; i < SEQ_SLOTS; i++) get_pattern(r, &st->slots[i], true);
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
}

bool storage_save(const char *path, const SaveState *st) {
    Writer w = {.ok = true};
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
    uint8_t buf[MAX_PAYLOAD];
    uint32_t sum = 0;
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "VIBE", 4) == 0 && fread(head, sizeof(head), 1, f) == 1 &&
              (head[0] >= 1 && head[0] <= VERSION) && head[1] <= MAX_PAYLOAD && fread(buf, 1, head[1], f) == head[1] &&
              fread(&sum, sizeof(sum), 1, f) == 1 && sum == checksum(buf, head[1]);
    fclose(f);
    if (!ok) return false;
    SaveState tmp;
    Reader r = {buf, head[1], 0, true};
    memset(&tmp, 0, sizeof(tmp));
    decode(&r, &tmp, head[0]);
    if (!r.ok) return false;
    *st = tmp;
    return true;
}
