/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- SDL2_image surface. Promoted from apps/SDLPoP.
 * One real entry point (IMG_Load / IMG_Load_RW); everything routes
 * through each consumer's build-time pre-converted PIMG blobs
 * (shim_image_pimg.c). Init/Quit/version are formalities so game init
 * sequences pass their checks; IMG_SavePNG is a stub.
 */

#ifndef PIZZA_SDL2_SDL_IMAGE_H
#define PIZZA_SDL2_SDL_IMAGE_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_IMAGE_MAJOR_VERSION 2
#define SDL_IMAGE_MINOR_VERSION 0
#define SDL_IMAGE_PATCHLEVEL    5

#define SDL_IMAGE_VERSION(v)                          \
	do {                                          \
		(v)->major = SDL_IMAGE_MAJOR_VERSION; \
		(v)->minor = SDL_IMAGE_MINOR_VERSION; \
		(v)->patch = SDL_IMAGE_PATCHLEVEL;    \
	} while (0)

#define IMG_INIT_JPG  0x00000001
#define IMG_INIT_PNG  0x00000002
#define IMG_INIT_TIF  0x00000004
#define IMG_INIT_WEBP 0x00000008

int IMG_Init(int flags);
void IMG_Quit(void);
const SDL_version *IMG_Linked_Version(void);

SDL_Surface *IMG_Load(const char *file);
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc);
const char *IMG_GetError(void);
int IMG_SavePNG(SDL_Surface *surface, const char *file);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2_SDL_IMAGE_H */
