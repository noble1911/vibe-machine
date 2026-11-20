// songs_probe -- named songs on disk: save, list (sorted, with summary),
// load, overwrite, delete, and name cleaning.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dice.h"
#include "samples.h"
#include "songs.h"

static int fails;
static void check(int ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static SaveState make(int bpm, int filled, const char *name) {
    SaveState st;
    memset(&st, 0, sizeof(st));
    st.bpm = bpm;
    for (int i = 0; i < SEQ_SLOTS; i++) st.slots[i].len = SEQ_STEPS;  // as the sequencer keeps them
    for (int i = 0; i < filled; i++) dice_groove(&st.slots[i], 99u + i, 0);
    strcpy(st.name, name);
    sampler_edit_default(&st.sample_edit);
    st.arp = (SeqArp){false, ARP_UP, ARP_16TH, 1};
    return st;
}

int main(void) {
    const char *root = "build/songs";
    mkdir("build", 0777);
    SongInfo list[SONGS_MAX];
    int n = song_list(root, list, SONGS_MAX, true);
    for (int i = 0; i < n; i++) song_delete(root, list[i].name);  // start clean

    char name[SONG_NAME_MAX + 1];
    song_clean_name("  neon/pizza!!  v2 ", name);
    check(strcmp(name, "NEON PIZZA V2") == 0, "names are cleaned to safe upper-case words");
    song_clean_name("???", name);
    check(strcmp(name, "UNTITLED") == 0, "an empty name becomes UNTITLED");
    song_clean_name("ABCDEFGHIJKLMNOPQRSTUVWXYZ", name);
    check(strlen(name) == SONG_NAME_MAX, "long names are cut to 16 characters");
    int ok = 1;
    for (uint32_t s = 1; s < 50; s++) {
        char r[SONG_NAME_MAX + 1], c[SONG_NAME_MAX + 1];
        song_random_name(s, r);
        song_clean_name(r, c);
        ok &= strcmp(r, c) == 0 && strlen(r) > 3;
    }
    check(ok, "random names are already clean");
    ok = 1;
    for (uint32_t s = 1; s <= 200000; s++) {
        char r[SONG_NAME_MAX + 1], c[SONG_NAME_MAX + 1];
        song_random_name(s, r);
        song_clean_name(r, c);
        ok &= r[strlen(r) - 1] != ' ' && strcmp(r, c) == 0;
    }
    check(ok, "no random name (of 200,000) ends in a space");
    song_clean_name("ABCDEFGHIJKLMNO PQ", name);
    check(strcmp(name, "ABCDEFGHIJKLMNO") == 0, "a word that would start at the limit is dropped whole");

    SaveState a = make(120, 3, "ZEBRA DISCO"), b = make(90, 1, "ACID TAPE 4");
    check(song_save_state(root, "ZEBRA DISCO", &a) && song_save_state(root, "ACID TAPE 4", &b), "two songs save");
    n = song_list(root, list, SONGS_MAX, true);
    check(n == 2 && strcmp(list[0].name, "ACID TAPE 4") == 0 && strcmp(list[1].name, "ZEBRA DISCO") == 0,
          "the list is sorted by name");
    check(list[1].bpm == 120 && list[1].patterns == 3 && list[0].patterns == 1, "each entry knows its tempo and patterns");

    SaveState back;
    check(song_load_state(root, "ZEBRA DISCO", &back) && memcmp(&back, &a, sizeof(a)) == 0, "a song loads back exactly");
    SaveState a2 = make(150, 2, "ZEBRA DISCO");
    song_save_state(root, "ZEBRA DISCO", &a2);
    n = song_list(root, list, SONGS_MAX, true);
    check(n == 2 && list[1].bpm == 150, "saving under the same name overwrites it");

    char wav[160];
    song_path(root, "ACID TAPE 4", "tape1.wav", wav, sizeof(wav));
    FILE *f = fopen(wav, "wb");
    fputs("x", f);
    fclose(f);
    check(song_delete(root, "ACID TAPE 4") && !song_exists(root, "ACID TAPE 4"), "delete removes the song and its tapes");
    n = song_list(root, list, SONGS_MAX, true);
    check(n == 1, "the list no longer shows it");

    // More songs than a directory read would return in one go, and more than
    // the old 64 limit: every one is listed, sorted.
    for (int i = 0; i < 70; i++) {
        char nm[SONG_NAME_MAX + 1];
        snprintf(nm, sizeof(nm), "SONG %02d", 69 - i);
        SaveState st = make(100 + i, 1, nm);
        song_save_state(root, nm, &st);
    }
    n = song_list(root, list, SONGS_MAX, true);
    int sorted = 1;
    for (int i = 1; i < n; i++) sorted &= strcmp(list[i - 1].name, list[i].name) < 0;
    check(n == 71 && sorted && strcmp(list[n - 1].name, "ZEBRA DISCO") == 0, "71 songs are all listed, in order");
    for (int i = 0; i < n; i++) song_delete(root, list[i].name);

    // The sample library: any .wav (any case) is listed under a clean name,
    // sorted; other files are ignored.
    {
        const char *sroot = "build/samples_test";
        mkdir(sroot, 0777);
        const char *files[] = {"kick loop.WAV", "VOX 1.wav", "notes.txt", "a.wav"};
        for (int i = 0; i < 4; i++) {
            char path[160];
            sample_path(sroot, files[i], path, sizeof(path));
            FILE *sf = fopen(path, "wb");
            fputs("x", sf);
            fclose(sf);
        }
        static SampleFile sl[16];
        int sn = sample_list(sroot, sl, 16);
        check(sn == 3 && strcmp(sl[0].name, "A") == 0 && strcmp(sl[1].name, "KICK LOOP") == 0 &&
                  strcmp(sl[1].file, "kick loop.WAV") == 0 && strcmp(sl[2].name, "VOX 1") == 0,
              "the sample library lists WAVs by name, whatever their case");
        char fname[64];
        sample_file_for("NEON GECKO 7", fname, sizeof(fname));
        check(strcmp(fname, "NEON GECKO 7.wav") == 0, "a saved sample is NAME.wav");
        for (int i = 0; i < 4; i++) {
            char path[160];
            sample_path(sroot, files[i], path, sizeof(path));
            remove(path);
        }
        rmdir(sroot);
    }

    printf(fails ? "\n%d song problem(s)\n" : "\nall songs OK\n", fails);
    return fails ? 1 : 0;
}
