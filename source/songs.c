// songs.c -- see songs.h.

#include "songs.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rng.h"

void song_clean_name(const char *in, char *out) {
    int n = 0;
    bool space = false;
    for (const char *p = in; *p && n < SONG_NAME_MAX; p++) {
        char c = (char)toupper((unsigned char)*p);
        if (isalnum((unsigned char)c) || c == '-') {
            if (space && n > 0) out[n++] = ' ';
            if (n < SONG_NAME_MAX) out[n++] = c;
            space = false;
        } else {
            space = true;  // anything else becomes a single space
        }
    }
    out[n] = '\0';
    if (n == 0) strcpy(out, "UNTITLED");
}

void song_random_name(uint32_t seed, char *out) {
    static const char *const adj[] = {"NEON", "TURBO", "COSMIC", "FUZZY", "LASER", "PIXEL", "MEGA", "HYPER",
                                      "ACID", "DISCO", "ROBO", "JELLY", "VELVET", "RADICAL", "CHROME", "SPACE"};
    static const char *const noun[] = {"GECKO", "PIZZA", "SHARK", "PALM", "DOLPHIN", "TAPE", "MANGO", "COMET",
                                       "LLAMA", "WAVE", "KITTEN", "CASSETTE", "PRISM", "ZEBRA", "DONUT", "BOT"};
    Rng r = {seed | 1u};
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s %d", adj[rng_int(&r, 16)], noun[rng_int(&r, 16)], 1 + rng_int(&r, 99));
    song_clean_name(buf, out);
}

void song_path(const char *root, const char *name, const char *file, char *out, size_t len) {
    if (file)
        snprintf(out, len, "%s/%s/%s", root, name, file);
    else
        snprintf(out, len, "%s/%s", root, name);
}

bool song_save_state(const char *root, const char *name, const SaveState *st) {
    char path[160];
    mkdir(root, 0777);
    song_path(root, name, NULL, path, sizeof(path));
    mkdir(path, 0777);
    song_path(root, name, "song.vms", path, sizeof(path));
    return storage_save(path, st);
}

bool song_load_state(const char *root, const char *name, SaveState *st) {
    char path[160];
    song_path(root, name, "song.vms", path, sizeof(path));
    return storage_load(path, st);
}

bool song_exists(const char *root, const char *name) {
    char path[160];
    song_path(root, name, "song.vms", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

bool song_delete(const char *root, const char *name) {
    static const char *const files[] = {"song.vms", "tape1.wav", "tape2.wav", "tape3.wav", "tape4.wav", "mic.wav"};
    char path[160];
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        song_path(root, name, files[i], path, sizeof(path));
        remove(path);
    }
    song_path(root, name, NULL, path, sizeof(path));
    return rmdir(path) == 0;
}

static int by_name(const void *a, const void *b) {
    return strcmp(((const SongInfo *)a)->name, ((const SongInfo *)b)->name);
}

int song_list(const char *root, SongInfo *out, int max) {
    DIR *d = opendir(root);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.') continue;
        char clean[SONG_NAME_MAX + 1];
        song_clean_name(e->d_name, clean);
        if (strcmp(clean, e->d_name) != 0) continue;  // not one of ours
        SaveState st;
        if (!song_load_state(root, clean, &st)) continue;  // clean == d_name, and short
        SongInfo *s = &out[n++];
        strcpy(s->name, clean);
        s->bpm = st.bpm;
        s->patterns = 0;
        for (int i = 0; i < SEQ_SLOTS; i++) {
            const SeqPattern *p = &st.slots[i];
            for (int k = 0; k < SEQ_STEPS; k++)
                if (p->drums[k] || p->synth[k].count) {
                    s->patterns++;
                    break;
                }
        }
    }
    closedir(d);
    qsort(out, (size_t)n, sizeof(SongInfo), by_name);
    return n;
}
