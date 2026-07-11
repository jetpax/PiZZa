# SPDX-License-Identifier: Apache-2.0
#
# Host round-trip test: bake the real asset pack, read it back through
# the real device-side parsers (shim_tmx / shim_ttf_atlas /
# shim_image_pimg + surfaces).

CC ?= cc
PYTHON ?= python3
OUT := out

SHIM := ../../../lib/sdl2shim

CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	-I$(SHIM)/include -I$(SHIM) -I$(SHIM)/src -I../../include

SRCS := harness.c \
	../../src/shim_tmx.c \
	$(SHIM)/src/shim_surface.c \
	$(SHIM)/src/shim_ttf_atlas.c \
	$(SHIM)/src/shim_image_pimg.c \
	$(SHIM)/src/shim_rw.c

all: test

$(OUT)/sokoban_assets.bin: ../../tools/pack_assets.py \
		$(shell find ../../game/data -type f)
	@mkdir -p $(OUT)
	$(PYTHON) ../../tools/pack_assets.py ../../game/data $@ \
		--font "Font/ARCADE_N.TTF@8"

$(OUT)/pack_host_test: $(SRCS)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: $(OUT)/pack_host_test $(OUT)/sokoban_assets.bin
	./$(OUT)/pack_host_test $(OUT)/sokoban_assets.bin

clean:
	rm -rf $(OUT)

.PHONY: all test clean
