/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host round-trip test for the sokoban asset pipeline: the REAL pack
 * (built by tools/pack_assets.py from the vendored game data) is read
 * back through the REAL device-side parsers -- shim_tmx.c (S2TM),
 * shim_ttf_atlas.c (S2SA), shim_image_pimg.c (PIMG). The pack-format
 * reader here is an independent second implementation of the SOKPACK1
 * layout, so it also checks the format contract against sok_assets.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include "sdl2shim.h"
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <tmx.h>

/* ── seams ───────────────────────────────────────────────────── */

static char error_buf[192];

void s2s_set_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(error_buf, sizeof(error_buf), fmt, ap);
	va_end(ap);
}

const char *SDL_GetError(void)
{
	return error_buf;
}

/* independent SOKPACK1 reader over the pack file */
static uint8_t *pack;
static size_t pack_size;

struct pack_entry {
	uint32_t path_off;
	uint32_t data_off;
	uint32_t size;
} __attribute__((__packed__));

const void *s2s_asset_find(const char *path, size_t *size)
{
	if (memcmp(pack, "SOKPACK1", 8) != 0) {
		return NULL;
	}

	uint32_t count;

	memcpy(&count, pack + 8, 4);

	const struct pack_entry *e = (const struct pack_entry *)(pack + 12);

	for (uint32_t i = 0; i < count; i++) {
		if (strcmp((const char *)pack + e[i].path_off, path) == 0) {
			if (size != NULL) {
				*size = e[i].size;
			}
			return pack + e[i].data_off;
		}
	}
	return NULL;
}

static int failures;

#define CHECK(cond, ...)                                              \
	do {                                                          \
		if (!(cond)) {                                        \
			failures++;                                   \
			fprintf(stderr, "FAIL %s:%d: ", __func__, __LINE__); \
			fprintf(stderr, __VA_ARGS__);                 \
			fputc('\n', stderr);                          \
		}                                                     \
	} while (0)

/* ── tests ───────────────────────────────────────────────────── */

static void test_pimg_tileset(void)
{
	SDL_Surface *surf = IMG_Load("sokoban_tiles.png");

	CHECK(surf != NULL, "IMG_Load: %s", SDL_GetError());
	if (surf == NULL) {
		return;
	}
	CHECK(surf->w == 216 && surf->h == 36, "dims %dx%d", surf->w, surf->h);
	CHECK(surf->format->BitsPerPixel == 32, "RGBA png -> 32bpp surface");
	CHECK(surf->format->Amask == 0xFF000000u, "Amask");
	SDL_FreeSurface(surf);
}

static void test_font_atlas(void)
{
	TTF_Font *font = TTF_OpenFont("Font/ARCADE_N.TTF", 8);

	CHECK(font != NULL, "TTF_OpenFont: %s", SDL_GetError());
	if (font == NULL) {
		return;
	}

	int h = TTF_FontHeight(font);

	CHECK(h >= 8 && h <= 16, "plausible line height %d", h);

	SDL_Color white = { 255, 255, 255, 255 };
	SDL_Surface *surf = TTF_RenderText_Blended(font, "SOKOBAN", white);

	CHECK(surf != NULL, "RenderText: %s", SDL_GetError());
	if (surf != NULL) {
		CHECK(surf->w >= 7 * 4, "text width %d", surf->w);

		int lit = 0;

		for (int y = 0; y < surf->h; y++) {
			const uint32_t *row = (const uint32_t *)
				((const uint8_t *)surf->pixels + (size_t)y * surf->pitch);

			for (int x = 0; x < surf->w; x++) {
				if ((row[x] >> 24) > 0x40) {
					lit++;
				}
			}
		}
		CHECK(lit > 50, "glyph coverage lit=%d", lit);
		SDL_FreeSurface(surf);
	}
	TTF_CloseFont(font);
}

static void test_tmx_level1(void)
{
	tmx_img_load_func = (void *(*)(const char *))IMG_Load;
	tmx_img_free_func = (void (*)(void *))SDL_FreeSurface;

	tmx_map *map = tmx_load("Maps/Original/level_001.tmx");

	CHECK(map != NULL, "tmx_load: %s", SDL_GetError());
	if (map == NULL) {
		return;
	}

	CHECK(map->width == 20 && map->height == 11, "map %ux%u",
	      map->width, map->height);
	CHECK(map->tile_width == 36 && map->tile_height == 36, "tile dims");
	CHECK(map->backgroundcolor == 0x262626, "bg %06x", map->backgroundcolor);

	int n_tile_layers = 0, n_obj_layers = 0, n_objects = 0, n_man = 0;
	tmx_layer *ly = map->ly_head;
	uint32_t *gids = NULL;
	tmx_object_group *objgr = NULL;

	while (ly != NULL) {
		if (ly->type == L_LAYER && ly->visible) {
			n_tile_layers++;
			gids = ly->content.gids;
		} else if (ly->type == L_OBJGR) {
			n_obj_layers++;
			objgr = ly->content.objgr;
		}
		ly = ly->next;
	}
	CHECK(n_tile_layers == 1, "tile layers %d", n_tile_layers);
	CHECK(n_obj_layers == 1, "obj layers %d", n_obj_layers);

	if (gids != NULL) {
		/* the man's start tile (11,8) is floor (gid 2); its left
		 * neighbour is wall (gid 1)
		 */
		CHECK((gids[8 * 20 + 11] & TMX_FLIP_BITS_REMOVAL) == 2, "start tile");
		CHECK((gids[8 * 20 + 10] & TMX_FLIP_BITS_REMOVAL) == 1, "wall left");
	}

	if (objgr != NULL) {
		for (tmx_object *o = objgr->head; o != NULL; o = o->next) {
			CHECK(o->obj_type == OT_TILE, "obj type");
			n_objects++;
			if (o->content.gid == 3) {
				n_man++;
				CHECK((int)o->x == 396 && (int)o->y == 324,
				      "man at %d,%d", (int)o->x, (int)o->y);
			}
		}
	}
	CHECK(n_objects == 13, "objects %d", n_objects);
	CHECK(n_man == 1, "man count %d", n_man);

	/* tileset + per-gid tile lookup (Box gid 4 = 4th tile, ul_x 108) */
	CHECK(map->ts_head != NULL && map->ts_head->tileset != NULL, "tileset");

	tmx_tile *tile = tmx_get_tile(map, 4);

	CHECK(tile != NULL, "get_tile(4)");
	if (tile != NULL) {
		CHECK(tile->ul_x == 108 && tile->ul_y == 0, "tile 4 ul %u,%u",
		      tile->ul_x, tile->ul_y);
		CHECK(tile->tileset->tile_width == 36, "tile w");
	}

	/* the tileset image loaded through the hook */
	CHECK(map->ts_head->tileset->image->resource_image != NULL, "image hook");

	SDL_Surface *img = map->ts_head->tileset->image->resource_image;

	CHECK(img->w == 216 && img->h == 36, "tileset image dims");

	tmx_map_free(map);

	/* all eight maps load */
	static const char *const maps[] = {
		"test.tmx",
		"Maps/Original/level_001.tmx", "Maps/Original/level_002.tmx",
		"Maps/Original/level_003.tmx", "Maps/Original/level_004.tmx",
		"Maps/Microban/level_001.tmx", "Maps/Microban/level_002.tmx",
		"Maps/Microban/level_003.tmx",
	};

	for (size_t i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
		tmx_map *m = tmx_load(maps[i]);

		CHECK(m != NULL, "%s: %s", maps[i], SDL_GetError());
		tmx_map_free(m);
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <sokoban_assets.bin>\n", argv[0]);
		return 2;
	}

	FILE *f = fopen(argv[1], "rb");

	if (f == NULL) {
		fprintf(stderr, "cannot open %s\n", argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	pack_size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	pack = malloc(pack_size);
	if (fread(pack, 1, pack_size, f) != pack_size) {
		fprintf(stderr, "short read\n");
		return 2;
	}
	fclose(f);

	test_pimg_tileset();
	test_font_atlas();
	test_tmx_level1();

	if (failures != 0) {
		fprintf(stderr, "%d FAILURES\n", failures);
		return 1;
	}
	printf("pack_host: all tests passed (%zu byte pack)\n", pack_size);
	return 0;
}
