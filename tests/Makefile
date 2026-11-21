# Host-side checks: build AMY natively and exercise Vibe Machine's sounds
# and effects without a 3DS.   make -C tests
AMY := ../third_party/amy/src
AMY_SRC := $(addprefix $(AMY)/,amy.c algorithms.c api.c amy_midi.c custom.c cv_trigger.c \
	delay.c envelope.c examples.c filters.c instrument.c interp_partials.c log2_exp2.c \
	midi_mappings.c note_output.c oscillators.c parse.c patches.c pcm.c sequencer.c transfer.c)
AMY_OBJ := $(patsubst $(AMY)/%.c,build/amy-g/%.o,$(AMY_SRC))
CFLAGS := -O2 -DAMY_NO_MINIAUDIO -DAMY_WAVETABLE -DBLOCK_SIZE_BITS=7 -DGAMMA9001 -I$(AMY) -I../source
LIBS := -lm -lpthread

.PHONY: all clean
all: build/dual_probe build/sound_probe build/fx_probe build/logic_probe build/tape_probe build/seq_probe build/songs_probe build/mix_probe
	./build/sound_probe
	./build/fx_probe
	./build/logic_probe
	./build/tape_probe
	./build/seq_probe
	./build/songs_probe
	./build/mix_probe
	./build/dual_probe

build/amy-g/%.o: $(AMY)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

build/sound_probe: sound_probe.c probe_platform.c ../source/sounds.c ../source/synthedit.c ../source/sampler.c ../source/music.c ../source/drums.c $(AMY_OBJ) ../source/drawwave.c | build/drums.bin
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/fx_probe: fx_probe.c ../source/fx.c ../source/punch.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/logic_probe: logic_probe.c ../source/dice.c ../source/storage.c ../source/music.c ../source/sampler.c ../source/wav.c ../source/synthedit.c ../source/drawwave.c ../source/presets.c ../source/drums.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/tape_probe: tape_probe.c ../source/tape.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/seq_probe: seq_probe.c ../source/seq.c ../source/music.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/songs_probe: songs_probe.c ../source/samples.c ../source/sampler.c ../source/songs.c ../source/storage.c ../source/dice.c ../source/music.c ../source/synthedit.c ../source/drums.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/mix_probe: mix_probe.c probe_platform.c ../source/mixer.c ../source/sounds.c ../source/synthedit.c ../source/sampler.c ../source/music.c $(AMY_OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/inspect_song: inspect_song.c ../source/storage.c ../source/sampler.c ../source/music.c ../source/synthedit.c ../source/drums.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

build/bench_probe: bench_probe.c probe_platform.c ../source/mixer.c ../source/sounds.c ../source/synthedit.c ../source/sampler.c ../source/music.c $(AMY_OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

# AMY's drum banks (the app has them in its romfs), for the kit checks.
build/drums.bin: ../tools/make-drums.sh
	sh ../tools/make-drums.sh ../third_party/amy $@

# The 3DS build's two-core AMY (tools/amy-dualcore.sh), for dual_probe.
AMY_DUAL := build/amy-dual-src
AMY_DUAL_OBJ := $(patsubst $(AMY)/%.c,build/amy-dual-g/%.o,$(AMY_SRC))
$(AMY_DUAL)/amy.h: $(wildcard $(AMY)/*.c $(AMY)/*.h) ../tools/amy-dualcore.sh
	sh ../tools/amy-dualcore.sh $(AMY) $(AMY_DUAL)
build/amy-dual-g/%.o: $(AMY_DUAL)/amy.h
	@mkdir -p $(dir $@)
	$(CC) $(subst -I$(AMY),-I$(AMY_DUAL),$(CFLAGS)) -DVM_AMY_DUALCORE -w -c $(AMY_DUAL)/$*.c -o $@

build/dual_probe: dual_probe.c probe_platform.c ../source/amy_split.c ../source/mixer.c ../source/sounds.c ../source/synthedit.c ../source/sampler.c ../source/music.c $(AMY_DUAL_OBJ)
	@mkdir -p build
	$(CC) $(subst -I$(AMY),-I$(AMY_DUAL),$(CFLAGS)) -DVM_AMY_DUALCORE -Wall -Wextra $^ $(LIBS) -o $@

clean:
	rm -rf build
