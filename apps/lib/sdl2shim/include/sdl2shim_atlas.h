/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- baked glyph-atlas format ("S2SA").
 *
 * The TTF shim (shim_ttf_atlas.c) renders text from pre-baked glyph
 * atlases instead of carrying freetype. Each consumer's asset packer
 * bakes one atlas per (font file, point size) the game opens and
 * stores it in the asset pack under the key "<font path>@<size>"
 * (e.g. "Font/ARCADE_N.TTF@8"), which is what TTF_OpenFont looks up.
 *
 * Layout (LE, packed):
 *   struct s2sa_header
 *   struct s2sa_glyph glyphs[ncp]     (codepoints first_cp..first_cp+ncp-1)
 *   uint8_t coverage[atlas_w*atlas_h] (A8; color is applied at render)
 *
 * The Python baker (apps/<game>/tools/) must mirror this layout
 * exactly; the host round-trip test is the contract check.
 */

#ifndef PIZZA_SDL2SHIM_ATLAS_H
#define PIZZA_SDL2SHIM_ATLAS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S2SA_MAGIC "S2SA"

struct s2sa_header {
	uint8_t magic[4];     /* "S2SA" */
	uint16_t first_cp;    /* first codepoint (32 for printable ASCII) */
	uint16_t ncp;         /* glyph count (95 for printable ASCII) */
	int16_t ascent;       /* px above baseline */
	int16_t descent;      /* px below baseline; line height = ascent+descent */
	uint16_t atlas_w;
	uint16_t atlas_h;
	uint16_t px_size;     /* nominal point size the font was baked at */
	uint16_t rsvd;
} __attribute__((__packed__));

struct s2sa_glyph {
	uint16_t x;           /* atlas position */
	uint16_t y;
	uint8_t w;            /* coverage bitmap dims */
	uint8_t h;
	int8_t xoff;          /* placement: pen_x + xoff, top_of_line + yoff */
	int8_t yoff;
	int8_t advance;       /* pen advance after this glyph */
	uint8_t rsvd;
} __attribute__((__packed__));

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2SHIM_ATLAS_H */
