// presets.c -- see presets.h. Format: "VMP1", a version byte, the fields
// one by one (little-endian, as on the 3DS and every build machine), and an
// FNV-1a checksum of everything before it.

#include "presets.h"

#include <stdio.h>
#include <string.h>

#include "storage.h"  // storage_write_file: a preset is never half-written

#define VERSION 1

static uint32_t fnv(const uint8_t *b, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

typedef struct {
    uint8_t buf[512];
    size_t n;
    bool ok;
} Buf;

static void put(Buf *b, const void *p, size_t n) {
    if (b->n + n > sizeof(b->buf)) {
        b->ok = false;
        return;
    }
    memcpy(b->buf + b->n, p, n);
    b->n += n;
}

static void get(Buf *b, void *p, size_t n) {
    if (b->n + n > sizeof(b->buf)) {
        b->ok = false;
        memset(p, 0, n);
        return;
    }
    memcpy(p, b->buf + b->n, n);
    b->n += n;
}

bool preset_save(const char *path, const Preset *p) {
    Buf b = {.ok = true};
    const uint8_t v = VERSION;
    const int32_t ints[2] = {p->sound, p->patch};
    put(&b, "VMP1", 4);
    put(&b, &v, 1);
    put(&b, ints, sizeof(ints));
    put(&b, p->macro, sizeof(p->macro));
    put(&b, p->edit.p, sizeof(p->edit.p));
    put(&b, &p->edit.set, 2);
    put(&b, &p->edit.lfo_target, 1);
    put(&b, &p->edit.mono, 1);
    put(&b, &p->edit.unison, 1);
    put(&b, p->wave.pt, DRAW_POINTS);
    const uint32_t sum = fnv(b.buf, b.n);
    put(&b, &sum, 4);
    if (!b.ok) return false;
    return storage_write_file(path, b.buf, b.n);
}

static bool load_one(const char *path, void *out_) {
    Preset *out = out_;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Buf b = {.ok = true};
    const size_t n = fread(b.buf, 1, sizeof(b.buf), f);
    fclose(f);
    if (n < 9) return false;
    uint32_t sum;
    memcpy(&sum, b.buf + n - 4, 4);
    if (memcmp(b.buf, "VMP1", 4) != 0 || fnv(b.buf, n - 4) != sum) return false;
    Preset p;
    memset(&p, 0, sizeof(p));
    b.n = 4;
    uint8_t v;
    int32_t ints[2];
    get(&b, &v, 1);
    get(&b, ints, sizeof(ints));
    get(&b, p.macro, sizeof(p.macro));
    get(&b, p.edit.p, sizeof(p.edit.p));
    get(&b, &p.edit.set, 2);
    get(&b, &p.edit.lfo_target, 1);
    get(&b, &p.edit.mono, 1);
    get(&b, &p.edit.unison, 1);
    get(&b, p.wave.pt, DRAW_POINTS);
    if (!b.ok || v != VERSION || b.n != n - 4) return false;
    p.sound = ints[0];
    p.patch = ints[1];
    edit_clamp(&p.edit);
    *out = p;
    return true;
}

bool preset_load(const char *path, Preset *out) { return storage_read_newest(path, load_one, out); }
