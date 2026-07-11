/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban port -- libtmx API subset over baked S2TM records.
 *
 * sdl2-sokoban loads its Tiled maps through baylej/tmx (libxml2 +
 * zlib). On the device the maps are pre-baked at build time
 * (tools/pack_assets.py) into compact S2TM records, and this header +
 * shim_tmx.c reimplement exactly the slice of the libtmx API the game
 * walks: one tile layer of gids, one object group of tile objects,
 * one tileset, tmx_get_tile. Member names and types mirror upstream
 * libtmx so the game sources compile unmodified.
 */

#ifndef PIZZA_SOKOBAN_TMX_H
#define PIZZA_SOKOBAN_TMX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TMX_FLIP_BITS_REMOVAL 0x1FFFFFFFu

enum tmx_layer_type {
	L_NONE,
	L_LAYER,
	L_OBJGR,
	L_IMAGE,
};

enum tmx_obj_type {
	OT_NONE,
	OT_SQUARE,
	OT_POLYGON,
	OT_POLYLINE,
	OT_ELLIPSE,
	OT_TILE,
};

typedef struct tmx_image {
	char *source;
	void *resource_image;   /* SDL_Surface* via tmx_img_load_func */
} tmx_image;

typedef struct tmx_shape {
	double **points;
	int points_len;
} tmx_shape;

typedef struct tmx_object {
	int visible;
	enum tmx_obj_type obj_type;
	double x, y;
	double width, height;
	union {
		int gid;
		tmx_shape *shape;
	} content;
	struct tmx_object *next;
} tmx_object;

typedef struct tmx_object_group {
	unsigned int color;
	tmx_object *head;
} tmx_object_group;

typedef struct tmx_tileset {
	unsigned int tile_width;
	unsigned int tile_height;
	tmx_image *image;
} tmx_tileset;

typedef struct tmx_tileset_list {
	unsigned int firstgid;
	tmx_tileset *tileset;
	struct tmx_tileset_list *next;
} tmx_tileset_list;

typedef struct tmx_tile {
	unsigned int id;
	tmx_tileset *tileset;
	unsigned int ul_x;
	unsigned int ul_y;
} tmx_tile;

typedef struct tmx_layer {
	int visible;
	double opacity;
	enum tmx_layer_type type;
	union {
		uint32_t *gids;
		tmx_object_group *objgr;
		tmx_image *image;
	} content;
	struct tmx_layer *next;
} tmx_layer;

typedef struct tmx_map {
	unsigned int width;
	unsigned int height;
	unsigned int tile_width;
	unsigned int tile_height;
	int backgroundcolor;    /* 0xRRGGBB */
	tmx_layer *ly_head;
	tmx_tileset_list *ts_head;
} tmx_map;

tmx_map *tmx_load(const char *path);
void tmx_map_free(tmx_map *map);
tmx_tile *tmx_get_tile(tmx_map *map, unsigned int gid);
void tmx_perror(const char *prefix);

extern void *(*tmx_img_load_func)(const char *path);
extern void (*tmx_img_free_func)(void *address);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SOKOBAN_TMX_H */
