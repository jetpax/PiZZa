# SPDX-License-Identifier: Apache-2.0
#
# Host sine-sim for the RetroPiZZa audio bridge (SPSC ring + resample).
AUDIO_SRC := ../../../lib/sdl2shim/src/audio
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -I$(AUDIO_SRC)
SRCS := harness.c $(AUDIO_SRC)/resample_48k.c

retro_sine_test: $(SRCS) $(AUDIO_SRC)/*.h
	mkdir -p out
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lm

.PHONY: run clean
run: retro_sine_test
	./retro_sine_test

clean:
	rm -rf retro_sine_test out
