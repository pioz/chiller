CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11

SRC = src/main.c src/app.c src/synth.c

ifeq ($(shell uname -s),Darwin)
SRC += src/audio_mac.c
LDLIBS = -lm -lpthread -framework AudioToolbox -framework CoreFoundation
else
SRC += src/audio_alsa.c
LDLIBS = -lm -lpthread -lasound
endif

chiller: $(SRC) src/app.h src/synth.h src/audio.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

run: chiller
	./chiller

clean:
	rm -f chiller *.wav

.PHONY: run clean web

# ---- web: the same engine compiled to WebAssembly (needs clang with the wasm32 target and wasm-ld)
WASM_CC ?= clang
WASM_SRC = src/synth.c src/app.c www/wasm/web.c www/wasm/libc.c
WASM_EXPORTS = web_buffer web_render web_sample_rate \
	app_init app_apply_scene app_press app_new_variation app_change_volume app_change_brightness \
	app_set_muted app_scene app_scene_name app_layer_key app_layer_name app_layer_on app_layer_detail \
	app_volume app_brightness app_key_name app_poll app_level app_chord app_breath_phase

web: www/chiller.wasm

www/chiller.wasm: $(WASM_SRC) src/app.h src/synth.h $(wildcard www/wasm/include/*.h)
	$(WASM_CC) --target=wasm32 -O2 -std=gnu11 -Wall -Wextra -ffreestanding -fno-builtin -nostdlib \
		-isystem www/wasm/include -Isrc -Wl,--no-entry -Wl,--strip-all \
		$(addprefix -Wl$(comma)--export=,$(WASM_EXPORTS)) -o $@ $(WASM_SRC)

comma := ,
