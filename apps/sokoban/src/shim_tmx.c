/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban port -- tmx_load over baked S2TM records.
 *
 * tools/pack_assets.py pre-parses each Tiled .tmx (XML + base64/zlib)
 * into an S2TM record at build time; loading a map on the device is a
 * header parse + struct build. The gid grid is used in place (it
 * points into the read-only pack); everything else is malloc'd and
 * freed by tmx_map_free.
 *
 * S2TM layout (LE, packed), mirrored by the baker:
 *   u8  magic[4] "S2TM"
 *   u16 width, height, tile_w, tile_h
 *   u32 backgroundcolor (0xRRGGBB)
 *   u16 nobjects, firstgid, tile_count, columns
 *   char image[64]                        pack key of the tileset image
 *   u32 gids[width*height]                flip bits preserved
 *   { u32 gid; i32 x, y, w, h; }[nobjects]
 *
 * Dual-target: builds under Zephyr and in the host pack test.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "s2s_compat.h"
#include "sdl2shim.h"
#include <tmx.h>

void *(*tmx_img_load_func)(const char *path);
void (*tmx_img_free_func)(void *address);

struct s2tm_header {
	uint8_t magic[4];
	uint16_t width, height;
	uint16_t tile_w, tile_h;
	uint32_t backgroundcolor;
	uint16_t nobjects;
	uint16_t firstgid;
	uint16_t tile_count;
	uint16_t columns;
	char image[64];
} __packed;

struct s2tm_obj {
	uint32_t gid;
	int32_t x, y, w, h;
} __packed;

/* One allocation carries every fixed-size piece; the object list and
 * tile table are sized per map.
 */
struct s2tm_runtime {
	tmx_map map;
	tmx_layer tile_layer;
	tmx_layer obj_layer;
	tmx_object_group objgr;
	tmx_tileset_list ts_list;
	tmx_tileset tileset;
	tmx_image image;
	tmx_object *objects;
	tmx_tile *tiles;
	unsigned int tile_count;
};

tmx_map *tmx_load(const char *path)
{
	size_t size;
	const uint8_t *blob = s2s_asset_find(path, &size);

	if (blob == NULL) {
		s2s_set_error("tmx_load: %s not in asset pack", path);
		return NULL;
	}

	const struct s2tm_header *hdr = (const struct s2tm_header *)blob;

	if (size < sizeof(*hdr) || memcmp(hdr->magic, "S2TM", 4) != 0) {
		s2s_set_error("tmx_load: %s is not an S2TM record", path);
		return NULL;
	}

	size_t ntiles_grid = (size_t)hdr->width * hdr->height;
	size_t need = sizeof(*hdr) + ntiles_grid * 4 +
		      (size_t)hdr->nobjects * sizeof(struct s2tm_obj);

	if (size < need) {
		s2s_set_error("tmx_load: %s truncated (%zu < %zu)", path, size, need);
		return NULL;
	}

	struct s2tm_runtime *rt = calloc(1, sizeof(*rt));

	if (rt == NULL) {
		s2s_set_error("tmx_load: out of memory");
		return NULL;
	}
	rt->objects = calloc(hdr->nobjects ? hdr->nobjects : 1, sizeof(tmx_object));
	rt->tiles = calloc(hdr->tile_count ? hdr->tile_count : 1, sizeof(tmx_tile));
	if (rt->objects == NULL || rt->tiles == NULL) {
		free(rt->objects);
		free(rt->tiles);
		free(rt);
		s2s_set_error("tmx_load: out of memory");
		return NULL;
	}

	rt->map.width = hdr->width;
	rt->map.height = hdr->height;
	rt->map.tile_width = hdr->tile_w;
	rt->map.tile_height = hdr->tile_h;
	rt->map.backgroundcolor = (int)hdr->backgroundcolor;

	/* tileset + per-gid tile table (ul_x/ul_y from the column grid) */
	rt->tileset.tile_width = hdr->tile_w;
	rt->tileset.tile_height = hdr->tile_h;
	rt->tileset.image = &rt->image;
	rt->ts_list.firstgid = hdr->firstgid;
	rt->ts_list.tileset = &rt->tileset;
	rt->map.ts_head = &rt->ts_list;
	rt->tile_count = hdr->tile_count;

	for (unsigned int i = 0; i < hdr->tile_count; i++) {
		rt->tiles[i].id = i;
		rt->tiles[i].tileset = &rt->tileset;
		rt->tiles[i].ul_x = (i % hdr->columns) * hdr->tile_w;
		rt->tiles[i].ul_y = (i / hdr->columns) * hdr->tile_h;
	}

	/* the tile layer's gid grid lives in the (aligned) pack blob */
	rt->tile_layer.visible = 1;
	rt->tile_layer.opacity = 1.0;
	rt->tile_layer.type = L_LAYER;
	rt->tile_layer.content.gids = (uint32_t *)(blob + sizeof(*hdr));
	rt->tile_layer.next = &rt->obj_layer;

	const struct s2tm_obj *objs =
		(const struct s2tm_obj *)(blob + sizeof(*hdr) + ntiles_grid * 4);

	for (unsigned int i = 0; i < hdr->nobjects; i++) {
		tmx_object *o = &rt->objects[i];

		o->visible = 1;
		o->obj_type = OT_TILE;
		o->x = objs[i].x;
		o->y = objs[i].y;
		o->width = objs[i].w;
		o->height = objs[i].h;
		o->content.gid = (int)objs[i].gid;
		o->next = (i + 1 < hdr->nobjects) ? &rt->objects[i + 1] : NULL;
	}
	rt->objgr.color = 0;
	rt->objgr.head = hdr->nobjects ? &rt->objects[0] : NULL;

	rt->obj_layer.visible = 1;
	rt->obj_layer.opacity = 1.0;
	rt->obj_layer.type = L_OBJGR;
	rt->obj_layer.content.objgr = &rt->objgr;
	rt->obj_layer.next = NULL;

	rt->map.ly_head = &rt->tile_layer;

	if (tmx_img_load_func != NULL) {
		rt->image.resource_image = tmx_img_load_func(hdr->image);
		if (rt->image.resource_image == NULL) {
			s2s_set_error("tmx_load: tileset image '%s' failed", hdr->image);
			tmx_map_free(&rt->map);
			return NULL;
		}
	}

	return &rt->map;
}

void tmx_map_free(tmx_map *map)
{
	if (map == NULL) {
		return;
	}

	struct s2tm_runtime *rt = (struct s2tm_runtime *)map;

	if (rt->image.resource_image != NULL && tmx_img_free_func != NULL) {
		tmx_img_free_func(rt->image.resource_image);
	}
	free(rt->objects);
	free(rt->tiles);
	free(rt);
}

tmx_tile *tmx_get_tile(tmx_map *map, unsigned int gid)
{
	struct s2tm_runtime *rt = (struct s2tm_runtime *)map;

	gid &= TMX_FLIP_BITS_REMOVAL;
	if (gid < rt->ts_list.firstgid) {
		return NULL;
	}

	unsigned int id = gid - rt->ts_list.firstgid;

	if (id >= rt->tile_count) {
		return NULL;
	}
	return &rt->tiles[id];
}

void tmx_perror(const char *prefix)
{
	fprintf(stderr, "%s: %s\n", prefix, SDL_GetError());
}
