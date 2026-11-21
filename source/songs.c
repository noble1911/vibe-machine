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
            if (space && n > 0) {
                // A space only goes in if the next word's first letter fits
                // too: a name must never end in a space (FAT drops it, and
                // the list would no longer match the folder).
                if (n + 1 >= SONG_NAME_MAX) break;
                out[n++] = ' ';
            }
            out[n++] = c;
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
    return storage_exists(path);  // (or the copy a cut-off save left)
}

bool song_delete(const char *root, const char *name) {
    static const char *const files[] = {"song.vms", "song.vms.new", "song.vms.bak", "tape1.wav", "tape2.wav", "tape3.wav", "tape4.wav", "mic.wav",
                                        "drum1.wav", "drum2.wav", "drum3.wav", "drum4.wav", "drum5.wav", "drum6.wav",
                                        "drum7.wav", "drum8.wav", "part1.wav", "part2.wav", "part3.wav"};
    char path[160];
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        song_path(root, name, files[i], path, sizeof(path));
        remove(path);
    }
    song_path(root, name, NULL, path, sizeof(path));
    return rmdir(path) == 0;
}

typedef struct {
    char name[SONG_NAME_MAX + 1];
} Name;

static int by_plain_name(const void *a, const void *b) { return strcmp(((const Name *)a)->name, ((const Name *)b)->name); }

bool song_info(const char *root, SongInfo *s) {
    SaveState st;
    if (!song_load_state(root, s->name, &st)) return false;
    s->bpm = st.bpm;
    s->patterns = 0;
    for (int i = 0; i < SEQ_SLOTS; i++) {
        const SeqPattern *p = &st.slots[i];
        bool used = p->count > 0;
        for (int k = 0; k < SEQ_STEPS && !used; k++) used = p->drums[k] != 0;
        s->patterns += used;
    }
    return true;
}

int song_list(const char *root, SongInfo *out, int max, bool with_info) {
    // Gather every song folder first, sort, and only then keep the first
    // `max`: stopping at `max` in directory order would hide arbitrary songs.
    DIR *d = opendir(root);
    if (!d) return 0;
    int cap = 64, count = 0;
    Name *names = malloc(sizeof(Name) * (size_t)cap);
    struct dirent *e;
    while (names && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char clean[SONG_NAME_MAX + 1];
        song_clean_name(e->d_name, clean);
        if (strcmp(clean, e->d_name) != 0) continue;  // not one of ours
        if (count == cap) {
            Name *bigger = realloc(names, sizeof(Name) * (size_t)(cap *= 2));
            if (!bigger) break;
            names = bigger;
        }
        strcpy(names[count++].name, clean);
    }
    closedir(d);
    if (!names) return 0;
    qsort(names, (size_t)count, sizeof(Name), by_plain_name);
    int n = 0;
    for (int k = 0; k < count && n < max; k++) {
        SongInfo *s = &out[n];
        strcpy(s->name, names[k].name);
        s->bpm = -1;
        s->patterns = 0;
        if (with_info && !song_info(root, s)) continue;  // not a readable song
        n++;
    }
    free(names);
    return n;
}
