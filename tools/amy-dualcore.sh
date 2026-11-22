#!/bin/sh
# amy-dualcore.sh SRC DST -- a copy of AMY's source set up for the 3DS:
# its two-core rendering switched on for us (VM_AMY_DUALCORE), as AMY does
# for the ESP32 and RP2040, and its sample rate settable (VM_AMY_SAMPLE_RATE:
# the 3DS's DSP plays 32728 Hz, so rendering more than that is wasted). The submodule itself stays untouched at its pinned
# commit; the build compiles this copy. Files are only rewritten when they
# change, so a rebuild doesn't recompile AMY for nothing.
set -e
SRC="$1"
DST="$2"
mkdir -p "$DST"
for f in "$SRC"/*.c "$SRC"/*.h; do
    b=$(basename "$f")
    if [ "$b" = "amy.h" ]; then
        sed -e 's/^#if (defined (ESP_PLATFORM) ||/#if (defined (VM_AMY_DUALCORE) || defined (ESP_PLATFORM) ||/' \
            -e 's/^#define AMY_SAMPLE_RATE 44100 *$/#ifdef VM_AMY_SAMPLE_RATE\
#define AMY_SAMPLE_RATE VM_AMY_SAMPLE_RATE\
#else\
#define AMY_SAMPLE_RATE 44100\
#endif\
extern int vm_amy_pipeline;\
extern int vm_amy_mix_from;\
void vm_amy_merge_cores(void);\
void vm_amy_advance_time(void);/' "$f" > "$DST/.amy.h.new"
        grep -q 'defined (VM_AMY_DUALCORE)' "$DST/.amy.h.new" && grep -q 'VM_AMY_SAMPLE_RATE' "$DST/.amy.h.new" ||
            { echo "amy-dualcore.sh: amy.h changed upstream, update the patterns" >&2; exit 1; }
        if cmp -s "$DST/.amy.h.new" "$DST/amy.h"; then rm "$DST/.amy.h.new"; else mv "$DST/.amy.h.new" "$DST/amy.h"; fi
    elif [ "$b" = "amy.c" ]; then
        # The pipeline (engine.c): core 1 renders the NEXT block's
        # oscillators while core 0 mixes this one down. For that, the
        # mixdown must not read core 1's buffers (the engine merges them
        # itself, before core 1 starts again), nor move AMY's clock under
        # the renderer (the engine advances it at a fixed point). Both stay
        # as they were unless vm_amy_pipeline is set.
        sed -e 's/^        for (int16_t i=0; i < AMY_BLOCK_SIZE \* AMY_NCHANS; ++i)  fbl\[0\]\[bus\]\[i\] += fbl\[1\]\[bus\]\[i\];$/        for (int16_t i=0; !vm_amy_pipeline \&\& i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i)  fbl[0][bus][i] += fbl[1][bus][i];/' \
            -e 's/^    amy_global.total_blocks++;$/    if (!vm_amy_pipeline) amy_global.total_blocks++;/' \
            -e 's/^            for (int bus = 0; bus <= amy_global.highest_bus; ++bus) {$/            for (int bus = vm_amy_mix_from; bus <= amy_global.highest_bus; ++bus) {/' "$f" > "$DST/.amy.c.new"
        cat >> "$DST/.amy.c.new" <<'PIPE'

// --- Vibe Machine's pipeline hooks (tools/amy-dualcore.sh) ---
int vm_amy_pipeline = 0;
// The first bus the final mixdown adds up. The mixer's hook empties the
// strips' buses into the master, so the engine starts it at the master.
int vm_amy_mix_from = 0;
void vm_amy_merge_cores(void) {
    for (int bus = 0; bus <= amy_global.highest_bus; ++bus)
        for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; ++i) fbl[0][bus][i] += fbl[1][bus][i];
}
void vm_amy_advance_time(void) {
    amy_global.total_blocks++;
    amy_global.total_samples = amy_global.total_blocks * AMY_BLOCK_SIZE;
    amy_global.time = amy_global.total_samples * (1.0f / AMY_SAMPLE_RATE);
}
PIPE
        grep -q '!vm_amy_pipeline \&\& i < AMY_BLOCK_SIZE' "$DST/.amy.c.new" && grep -q 'if (!vm_amy_pipeline) amy_global.total_blocks++;' "$DST/.amy.c.new" &&
            grep -q 'for (int bus = vm_amy_mix_from; bus <= amy_global.highest_bus' "$DST/.amy.c.new" ||
            { echo "amy-dualcore.sh: amy.c changed upstream, update the patterns" >&2; exit 1; }
        if cmp -s "$DST/.amy.c.new" "$DST/amy.c"; then rm "$DST/.amy.c.new"; else mv "$DST/.amy.c.new" "$DST/amy.c"; fi
    elif ! cmp -s "$f" "$DST/$b"; then
        cp -p "$f" "$DST/$b"
    fi
done
