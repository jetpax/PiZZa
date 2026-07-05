# SPDX-License-Identifier: Apache-2.0
#
# Host harness for the HDMI-audio core (work order M0 gate). Compiles
# the freestanding src/audio modules with the host compiler and runs
# the bit-exact framing / resampler / ring / pump tests.

AUDIO_SRC := ../../src/audio

CFLAGS := -O2 -std=c11 -Wall -Wextra -I$(AUDIO_SRC)
SRCS := harness.c \
	$(AUDIO_SRC)/iec958_pack.c \
	$(AUDIO_SRC)/resample_48k.c \
	$(AUDIO_SRC)/ring.c \
	$(AUDIO_SRC)/audio_pump.c

audio_host_test: $(SRCS) $(AUDIO_SRC)/*.h
	mkdir -p out
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lm

.PHONY: run clean
run: audio_host_test
	./audio_host_test

clean:
	rm -rf audio_host_test out
