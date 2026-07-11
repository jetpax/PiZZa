/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- SDL2_ttf subset over baked glyph atlases.
 *
 * No freetype on the device: TTF_OpenFont resolves a pre-baked S2SA
 * atlas from the asset pack (see sdl2shim_atlas.h) and
 * TTF_RenderText_Blended rasterises strings from it. Only the calls
 * sdl2-sokoban makes exist; grow per consumer.
 */

#ifndef PIZZA_SDL2_SDL_TTF_H
#define PIZZA_SDL2_SDL_TTF_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_TTF_MAJOR_VERSION 2
#define SDL_TTF_MINOR_VERSION 0
#define SDL_TTF_PATCHLEVEL    18

#define SDL_TTF_VERSION(v)                          \
	do {                                        \
		(v)->major = SDL_TTF_MAJOR_VERSION; \
		(v)->minor = SDL_TTF_MINOR_VERSION; \
		(v)->patch = SDL_TTF_PATCHLEVEL;    \
	} while (0)

typedef struct TTF_Font TTF_Font;

int TTF_Init(void);
void TTF_Quit(void);
const SDL_version *TTF_Linked_Version(void);
const char *TTF_GetError(void);

TTF_Font *TTF_OpenFont(const char *file, int ptsize);
void TTF_CloseFont(TTF_Font *font);

SDL_Surface *TTF_RenderText_Blended(TTF_Font *font, const char *text, SDL_Color fg);
int TTF_SizeText(TTF_Font *font, const char *text, int *w, int *h);
int TTF_FontHeight(const TTF_Font *font);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2_SDL_TTF_H */
