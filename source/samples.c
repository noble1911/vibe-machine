// samples.c -- see samples.h.

#include "samples.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_wav(const char *f) {
    size_t n = strlen(f);
    if (n < 5) return false;
    const char *e = f + n - 4;
    return e[0] == '.' && tolower((unsigned char)e[1]) == 'w' && tolower((unsigned char)e[2]) == 'a' &&
           tolower((unsigned char)e[3]) == 'v';
}

static int by_name(const void *a, const void *b) {
    return strcmp(((const SampleFile *)a)->name, ((const SampleFile *)b)->name);
}

int sample_list(const char *root, SampleFile *out, int max) {
    DIR *d = opendir(root);
    if (!d) return 0;
    int cap = 64, count = 0;
    SampleFile *all = malloc(sizeof(SampleFile) * (size_t)cap);
    struct dirent *e;
    while (all && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !is_wav(e->d_name) || strlen(e->d_name) >= sizeof(all[0].file)) continue;
        if (count == cap) {
            SampleFile *bigger = realloc(all, sizeof(SampleFile) * (size_t)(cap *= 2));
            if (!bigger) break;
            all = bigger;
        }
        SampleFile *s = &all[count++];
        snprintf(s->file, sizeof(s->file), "%s", e->d_name);
        char base[64];
        snprintf(base, sizeof(base), "%.*s", (int)(strlen(e->d_name) - 4), e->d_name);
        song_clean_name(base, s->name);
    }
    closedir(d);
    if (!all) return 0;
    qsort(all, (size_t)count, sizeof(SampleFile), by_name);
    int n = count < max ? count : max;
    memcpy(out, all, sizeof(SampleFile) * (size_t)n);
    free(all);
    return n;
}

void sample_path(const char *root, const char *file, char *out, size_t len) { snprintf(out, len, "%s/%s", root, file); }

void sample_file_for(const char *name, char *file, size_t len) { snprintf(file, len, "%s.wav", name); }
