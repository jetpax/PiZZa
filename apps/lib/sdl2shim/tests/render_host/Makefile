# SPDX-License-Identifier: Apache-2.0
#
# Host build of the dual-target shim TUs (render / surface / ttf-atlas
# / image-pimg) + exact-value pixel tests. Same pattern as SDLPoP's
# tests/audio_host: plain cc, no SDL, no Zephyr.

CC ?= cc
OUT := out

CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	-I../../include -I../.. -I../../src

SRCS := harness.c \
	../../src/shim_render.c \
	../../src/shim_surface.c \
	../../src/shim_ttf_atlas.c \
	../../src/shim_image_pimg.c \
	../../src/shim_rw.c

all: $(OUT)/render_host_test

$(OUT)/render_host_test: $(SRCS) ../../include/SDL2/SDL.h \
		../../include/SDL2/SDL_ttf.h ../../include/SDL2/SDL_image.h \
		../../include/sdl2shim_atlas.h ../../src/s2s_compat.h \
		../../sdl2shim.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: all
	./$(OUT)/render_host_test

clean:
	rm -rf $(OUT)

.PHONY: all test clean
