// storage.c -- see storage.h.
//
// Format: "VIBE", u16 version, u16 payload length, then fields written one
// by one (little-endian, as the 3DS and every build machine are), then a
// simple checksum. Writing field by field keeps struct padding out of it.

#include "storage.h"

#include <stdio.h>
#include <string.h>

#define VERSION 1
#define MAX_PAYLOAD 1024

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

static void encode(Writer *w, const SaveState *st) {
    int32_t ints[] = {st->theme, st->sound, st->patch, st->octave, st->bpm};
    put(w, ints, sizeof(ints));
    put(w, st->macro, sizeof(st->macro));
    put(w, st->params, sizeof(st->params));
    put(w, st->fx_x, sizeof(st->fx_x));
    put(w, st->fx_y, sizeof(st->fx_y));
    put(w, st->pattern.drums, sizeof(st->pattern.drums));
    for (int s = 0; s < SEQ_STEPS; s++) {
        const SeqNote *n = &st->pattern.synth[s];
        put(w, &n->key, 1);
        put(w, &n->count, 1);
        put(w, n->notes, MUSIC_MAX_NOTES);
    }
}

static void decode(Reader *r, SaveState *st) {
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
    get(r, st->pattern.drums, sizeof(st->pattern.drums));
    for (int s = 0; s < SEQ_STEPS; s++) {
        SeqNote *n = &st->pattern.synth[s];
        get(r, &n->key, 1);
        get(r, &n->count, 1);
        get(r, n->notes, MUSIC_MAX_NOTES);
        if (n->count > MUSIC_MAX_NOTES) r->ok = false;
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
              head[0] == VERSION && head[1] <= MAX_PAYLOAD && fread(buf, 1, head[1], f) == head[1] &&
              fread(&sum, sizeof(sum), 1, f) == 1 && sum == checksum(buf, head[1]);
    fclose(f);
    if (!ok) return false;
    SaveState tmp;
    Reader r = {buf, head[1], 0, true};
    decode(&r, &tmp);
    if (!r.ok) return false;
    *st = tmp;
    return true;
}
