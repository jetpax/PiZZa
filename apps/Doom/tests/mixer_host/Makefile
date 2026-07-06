# SPDX-License-Identifier: Apache-2.0
#
# Offline host test for shim_mixer.c (work-order G3).
#   make run

CC ?= cc
LIB = ../../../lib/sdl2shim
CFLAGS = -Wall -Wextra -O1 -g -std=c11 \
         -I. -I$(LIB)/include -I$(LIB) \
         -DCONFIG_SDL2SHIM_LOG_LEVEL=0

SRC = harness.c ../../src/shim_mixer.c
DEPS = $(LIB)/include/SDL2/SDL_mixer.h $(LIB)/include/SDL2/SDL.h \
       zephyr/kernel.h zephyr/logging/log.h

mixer_host_test: $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $(SRC)

run: mixer_host_test
	./mixer_host_test

clean:
	rm -f mixer_host_test

.PHONY: run clean
