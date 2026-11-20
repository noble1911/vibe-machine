# Host-side checks: build AMY natively and exercise Vibe Machine's sounds
# and effects without a 3DS.   make -C tests
AMY := ../third_party/amy/src
AMY_SRC := $(addprefix $(AMY)/,amy.c algorithms.c api.c amy_midi.c custom.c cv_trigger.c \
	delay.c envelope.c examples.c filters.c instrument.c interp_partials.c log2_exp2.c \
	midi_mappings.c note_output.c oscillators.c parse.c patches.c pcm.c sequencer.c transfer.c)
AMY_OBJ := $(patsubst $(AMY)/%.c,build/amy/%.o,$(AMY_SRC))
CFLAGS := -O2 -DAMY_NO_MINIAUDIO -DAMY_WAVETABLE -DBLOCK_SIZE_BITS=7 -I$(AMY) -I../source
LIBS := -lm -lpthread

.PHONY: all clean
all: build/sound_probe
	./build/sound_probe

build/amy/%.o: $(AMY)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

build/sound_probe: sound_probe.c probe_platform.c ../source/sounds.c $(AMY_OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) -Wall -Wextra $^ $(LIBS) -o $@

clean:
	rm -rf build
