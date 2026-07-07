/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- SDL2_image surface over the PIMG raw format.
 *
 * Per work-order §4 the asset packer pre-converts every PNG to a raw
 * PIMG blob at build time (indexed pixels + RGBA palette preserved),
 * so "loading an image" is a header parse + row copy -- no PNG
 * decoder on the device. Format defined in tools/pack_assets.py.
 *
 * IMG_SavePNG is USE_SCREENSHOT-only -> permanent stub.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "sdl2shim.h"
#include <SDL2/SDL_image.h>

/* SDLPoP's LE masks (types.h). */
#define POP_RMSK24 0x000000FF
#define POP_GMSK24 0x0000FF00
#define POP_BMSK24 0x00FF0000
#define POP_RMSK32 0x00FF0000
#define POP_GMSK32 0x0000FF00
#define POP_BMSK32 0x000000FF
#define POP_AMSK32 0xFF000000

enum pimg_kind {
	PIMG_INDEX8 = 0,
	PIMG_RGB24 = 1,
	PIMG_ARGB32 = 2,
};

struct pimg_header {
	uint8_t magic[4];
	uint16_t w;
	uint16_t h;
	uint8_t kind;
	uint8_t rsvd;
	uint16_t ncolors;
} __packed;

static SDL_Surface *pimg_to_surface(const uint8_t *blob, size_t size)
{
	const struct pimg_header *hdr = (const struct pimg_header *)blob;

	if (size < sizeof(*hdr) || memcmp(hdr->magic, "PIMG", 4) != 0) {
		s2s_set_error("IMG: not a PIMG blob (%zu bytes)", size);
		return NULL;
	}

	int w = sys_le16_to_cpu(hdr->w);
	int h = sys_le16_to_cpu(hdr->h);
	int ncolors = sys_le16_to_cpu(hdr->ncolors);
	const uint8_t *palette = blob + sizeof(*hdr);
	const uint8_t *pixels = palette + (size_t)ncolors * 4;
	SDL_Surface *surf = NULL;
	size_t row_bytes;

	switch (hdr->kind) {
	case PIMG_INDEX8:
		surf = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
		row_bytes = (size_t)w;
		break;
	case PIMG_RGB24:
		surf = SDL_CreateRGBSurface(0, w, h, 24,
					    POP_RMSK24, POP_GMSK24, POP_BMSK24, 0);
		row_bytes = (size_t)w * 3;
		break;
	case PIMG_ARGB32:
		surf = SDL_CreateRGBSurface(0, w, h, 32,
					    POP_RMSK32, POP_GMSK32, POP_BMSK32, POP_AMSK32);
		row_bytes = (size_t)w * 4;
		break;
	default:
		s2s_set_error("IMG: unknown PIMG kind %d", hdr->kind);
		return NULL;
	}

	if (surf == NULL) {
		return NULL;
	}
	if ((size_t)(pixels - blob) + row_bytes * h > size) {
		s2s_set_error("IMG: truncated PIMG (%dx%d kind %d, %zu bytes)",
			      w, h, hdr->kind, size);
		SDL_FreeSurface(surf);
		return NULL;
	}

	for (int y = 0; y < h; y++) {
		memcpy((Uint8 *)surf->pixels + (size_t)y * surf->pitch,
		       pixels + (size_t)y * row_bytes, row_bytes);
	}

	if (hdr->kind == PIMG_INDEX8) {
		SDL_Color colors[256];
		int n = MIN(ncolors, 256);

		for (int i = 0; i < n; i++) {
			colors[i].r = palette[i * 4 + 0];
			colors[i].g = palette[i * 4 + 1];
			colors[i].b = palette[i * 4 + 2];
			colors[i].a = palette[i * 4 + 3];
		}
		SDL_SetPaletteColors(surf->format->palette, colors, 0, n);
	}
	return surf;
}

SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc)
{
	if (src == NULL) {
		s2s_set_error("IMG_Load_RW: NULL rwops");
		return NULL;
	}

	Sint64 size = src->size(src);
	SDL_Surface *surf = NULL;

	if (size > 0) {
		/* Memory rwops expose their buffer directly -- the common
		 * path from load_image (RWFromConstMem over pack data).
		 */
		if (src->mem.base != NULL &&
		    src->mem.stop - src->mem.base == (ptrdiff_t)size) {
			surf = pimg_to_surface(src->mem.base, (size_t)size);
		} else {
			Uint8 *buf = malloc((size_t)size);

			if (buf != NULL) {
				src->seek(src, 0, RW_SEEK_SET);
				src->read(src, buf, 1, (size_t)size);
				surf = pimg_to_surface(buf, (size_t)size);
				free(buf);
			} else {
				s2s_set_error("IMG_Load_RW: out of memory");
			}
		}
	} else {
		s2s_set_error("IMG_Load_RW: empty stream");
	}

	if (freesrc) {
		SDL_RWclose(src);
	}
	return surf;
}

SDL_Surface *IMG_Load(const char *file)
{
	size_t size;
	const void *blob = s2s_asset_find(file, &size);

	if (blob == NULL) {
		s2s_set_error("IMG_Load: %s not in asset pack", file);
		return NULL;
	}
	return pimg_to_surface(blob, size);
}

const char *IMG_GetError(void)
{
	return SDL_GetError();
}

int IMG_SavePNG(SDL_Surface *surface, const char *file)
{
	ARG_UNUSED(surface);
	ARG_UNUSED(file);
	s2s_set_error("IMG_SavePNG: screenshots not supported");
	return -1;
}
