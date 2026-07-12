/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Core-local fopen/fclose for the sokoban llext build.
 *
 * In the APP build, picolibc tinystdio funnels the game's fopen
 * through open(), which sok_assets.c serves from the embedded pack.
 * In the CORE build fopen would import from the FRONTEND's picolibc,
 * whose open() knows nothing about the pack baked into this llext --
 * so provide fopen/fclose here and resolve them at partial link.
 *
 * Game.cpp only ever uses fopen as a level-EXISTENCE probe
 * (fopen -> NULL-check -> fclose, game/src/Game.cpp:62-68), so a
 * dummy non-NULL handle is contractually sufficient. Anything that
 * ever fread()s would need the full fd layer instead -- fail loudly
 * if that day comes.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "sdl2shim.h"

FILE *fopen(const char *path, const char *mode)
{
	size_t size;

	(void)mode;
	if (s2s_asset_find(path, &size) != NULL) {
		return (FILE *)(uintptr_t)0x500BE;
	}
	return NULL;
}

int fclose(FILE *f)
{
	(void)f;
	return 0;
}
