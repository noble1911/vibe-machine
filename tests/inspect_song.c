// inspect_song -- print what a saved song (.vms) holds, on the build
// machine: patterns, parts, mixer, arrangement, FX moves. Handy for
// checking what the story build (make STORY=1) actually made.
//   make -C tests build/inspect_song && tests/build/inspect_song path/to/song.vms
#include <stdio.h>
#include <string.h>

#include "storage.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    static SaveState st;
    if (!storage_load(argv[1], &st)) {
        printf("can't load %s\n", argv[1]);
        return 1;
    }
    printf("name \"%s\"  bpm %d  swing %.2f  song %d  slot %c  part %d  muted 0x%x\n", st.name, st.bpm, st.swing, st.song,
           'A' + st.slot, st.part + 1, st.muted);
    for (int k = 0; k < PARTS; k++)
        printf("part %d: sound %d patch %d macros %.2f %.2f\n", k + 1, st.parts[k].sound, st.parts[k].patch,
               st.parts[k].macro[0], st.parts[k].macro[1]);
    for (int i = 0; i < SEQ_SLOTS; i++) {
        const SeqPattern *p = &st.slots[i];
        int hits = 0, fx = 0;
        for (int s = 0; s < SEQ_STEPS_MAX; s++) {
            for (int l = 0; l < 8; l++) hits += (p->drums[s] >> l) & 1;
            fx += p->fx[s] != 0;
        }
        if (!hits && !p->count && !fx) continue;
        printf("slot %c: %d bar(s), %d drum hits, %d notes, %d FX steps\n", 'A' + i, seq_bars(p), hits, p->count, fx);
        for (int l = 0; l < 4; l++) {
            printf("   lane %d ", l);
            for (int s = 0; s < seq_len(p); s++) putchar((p->drums[s] >> l) & 1 ? 'x' : '.');
            putchar('\n');
        }
        for (int n = 0; n < p->count; n++)
            printf("   note part %d step %2d pitch %d len %d vel %d\n", p->notes[n].part + 1, p->notes[n].step + 1,
                   p->notes[n].pitch, p->notes[n].len, p->notes[n].vel);
        if (fx) {
            printf("   fx ");
            for (int s = 0; s < seq_len(p); s++) putchar(p->fx[s] ? '0' + p->fx[s] : '.');
            putchar('\n');
        }
    }
    for (int k = 0; k < MIX_STRIPS; k++)
        printf("strip %d: level %.2f drive %.2f crush %.2f tone %.2f pan %.2f echo %.2f verb %.2f\n", k + 1, st.mix[k].level,
               st.mix[k].knob[MIX_DRIVE], st.mix[k].knob[MIX_CRUSH], st.mix[k].knob[MIX_TONE], st.mix[k].knob[MIX_PAN],
               st.mix[k].knob[MIX_ECHO], st.mix[k].knob[MIX_VERB]);
    printf("key %d scale %d; arp %s mode %d rate %d octaves %d\n", st.key_root, st.key_scale, st.arp.on ? "on" : "off",
           st.arp.mode, st.arp.rate, st.arp.octaves);
    printf("arrangement: %d rows, %s\n", st.arr.count, st.arr.loop ? "LOOP" : "STOP");
    for (int i = 0; i < st.arr.count; i++)
        printf("   %c x%d mutes 0x%x\n", 'A' + st.arr.rows[i].slot, st.arr.rows[i].reps, st.arr.rows[i].mutes);
    return 0;
}
