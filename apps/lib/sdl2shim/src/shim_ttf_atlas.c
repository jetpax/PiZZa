/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- SDL2_ttf over baked S2SA glyph atlases.
 *
 * TTF_OpenFont("Font/X.TTF", 8) looks up the pack entry "Font/X.TTF@8"
 * (baked at build time; format in sdl2shim_atlas.h) and hands back a
 * handle over the read-only blob. TTF_RenderText_Blended composes an
 * ARGB8888 surface from the A8 coverage atlas: alpha = coverage, RGB =
 * the requested color -- classic SDL_ttf Blended semantics.
 *
 * Codepoints outside the baked range render as blank advance-width
 * boxes (missing-glyph behavior a menu can survive). Bytes >= 0x80 are
 * treated as one codepoint each (the baked range is ASCII anyway).
 *
 * Dual-target: builds under Zephyr and in the host tests. Requires the
 * surface TU (SDL_CreateRGBSurface) and an asset store (s2s_asset_find).
 */

#include "s2s_compat.h"
#include "sdl2shim.h"
#include <SDL2/SDL_ttf.h>
#include <sdl2shim_atlas.h>

#define S2S_MAX_FONTS 4

struct TTF_Font {
	const struct s2sa_header *hdr;
	const struct s2sa_glyph *glyphs;
	const uint8_t *coverage;
	bool in_use;
};

static struct TTF_Font fonts[S2S_MAX_FONTS];

int TTF_Init(void)
{
	return 0;
}

void TTF_Quit(void)
{
}

const SDL_version *TTF_Linked_Version(void)
{
	static const SDL_version v = { SDL_TTF_MAJOR_VERSION,
				       SDL_TTF_MINOR_VERSION,
				       SDL_TTF_PATCHLEVEL };
	return &v;
}

const char *TTF_GetError(void)
{
	return SDL_GetError();
}

TTF_Font *TTF_OpenFont(const char *file, int ptsize)
{
	char key[128];
	size_t size;

	snprintf(key, sizeof(key), "%s@%d", file, ptsize);

	const uint8_t *blob = s2s_asset_find(key, &size);

	if (blob == NULL) {
		s2s_set_error("TTF_OpenFont: no baked atlas '%s' in asset pack", key);
		return NULL;
	}
	if (size < sizeof(struct s2sa_header) ||
	    memcmp(blob, S2SA_MAGIC, 4) != 0) {
		s2s_set_error("TTF_OpenFont: '%s' is not an S2SA atlas", key);
		return NULL;
	}

	const struct s2sa_header *hdr = (const struct s2sa_header *)blob;
	size_t need = sizeof(*hdr) +
		      (size_t)hdr->ncp * sizeof(struct s2sa_glyph) +
		      (size_t)hdr->atlas_w * hdr->atlas_h;

	if (size < need) {
		s2s_set_error("TTF_OpenFont: '%s' truncated (%zu < %zu)", key, size, need);
		return NULL;
	}

	for (int i = 0; i < S2S_MAX_FONTS; i++) {
		if (fonts[i].in_use) {
			continue;
		}
		fonts[i].hdr = hdr;
		fonts[i].glyphs = (const struct s2sa_glyph *)(blob + sizeof(*hdr));
		fonts[i].coverage = (const uint8_t *)(fonts[i].glyphs + hdr->ncp);
		fonts[i].in_use = true;
		return &fonts[i];
	}

	s2s_set_error("TTF_OpenFont: font pool exhausted");
	return NULL;
}

void TTF_CloseFont(TTF_Font *font)
{
	if (font != NULL) {
		font->in_use = false;
	}
}

int TTF_FontHeight(const TTF_Font *font)
{
	return font->hdr->ascent + font->hdr->descent;
}

static const struct s2sa_glyph *find_glyph(const TTF_Font *font, unsigned char c)
{
	unsigned int cp = c;

	if (cp < font->hdr->first_cp || cp >= (unsigned int)font->hdr->first_cp + font->hdr->ncp) {
		return NULL;
	}
	return &font->glyphs[cp - font->hdr->first_cp];
}

static int measure_text(const TTF_Font *font, const char *text)
{
	int w = 0;

	for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
		const struct s2sa_glyph *g = find_glyph(font, *p);

		/* Missing glyphs advance by the average so layout survives. */
		w += (g != NULL) ? g->advance : font->hdr->px_size;
	}
	return w;
}

int TTF_SizeText(TTF_Font *font, const char *text, int *w, int *h)
{
	if (font == NULL || text == NULL) {
		s2s_set_error("TTF_SizeText: NULL arg");
		return -1;
	}
	if (w != NULL) {
		*w = measure_text(font, text);
	}
	if (h != NULL) {
		*h = TTF_FontHeight(font);
	}
	return 0;
}

SDL_Surface *TTF_RenderText_Blended(TTF_Font *font, const char *text, SDL_Color fg)
{
	if (font == NULL || text == NULL) {
		s2s_set_error("TTF_RenderText_Blended: NULL arg");
		return NULL;
	}

	int w = measure_text(font, text);
	int h = TTF_FontHeight(font);

	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}

	SDL_Surface *surf = SDL_CreateRGBSurface(0, w, h, 32,
						 0x00FF0000, 0x0000FF00,
						 0x000000FF, 0xFF000000);
	if (surf == NULL) {
		return NULL;
	}

	Uint32 rgb = ((Uint32)fg.r << 16) | ((Uint32)fg.g << 8) | (Uint32)fg.b;
	int pen_x = 0;

	for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
		const struct s2sa_glyph *g = find_glyph(font, *p);

		if (g == NULL) {
			pen_x += font->hdr->px_size;
			continue;
		}

		const uint8_t *cov = font->coverage +
				     (size_t)g->y * font->hdr->atlas_w + g->x;

		for (int gy = 0; gy < g->h; gy++) {
			int dy = g->yoff + gy;

			if (dy < 0 || dy >= h) {
				continue;
			}

			Uint32 *drow = (Uint32 *)((Uint8 *)surf->pixels +
						  (size_t)dy * surf->pitch);

			for (int gx = 0; gx < g->w; gx++) {
				int dx = pen_x + g->xoff + gx;
				uint8_t a = cov[(size_t)gy * font->hdr->atlas_w + gx];

				if (a == 0 || dx < 0 || dx >= w) {
					continue;
				}
				drow[dx] = ((Uint32)a << 24) | rgb;
			}
		}
		pen_x += g->advance;
	}

	return surf;
}
