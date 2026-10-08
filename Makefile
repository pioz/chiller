CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11

SRC = src/main.c src/synth.c

ifeq ($(shell uname -s),Darwin)
SRC += src/audio_mac.c
LDLIBS = -lm -lpthread -framework AudioToolbox -framework CoreFoundation
else
SRC += src/audio_alsa.c
LDLIBS = -lm -lpthread -lasound
endif

chiller: $(SRC) src/synth.h src/audio.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

run: chiller
	./chiller

clean:
	rm -f chiller *.wav

.PHONY: run clean
