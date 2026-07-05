/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- SDL2_image surface. One real entry point
 * (IMG_Load_RW); everything routes through the asset pipeline
 * decided in the work order §4 (build-time pre-converted blobs,
 * see shim_image.c). IMG_SavePNG is USE_SCREENSHOT-only -> stub.
 */

#ifndef PIZZA_SDL2_SDL_IMAGE_H
#define PIZZA_SDL2_SDL_IMAGE_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

SDL_Surface *IMG_Load(const char *file);
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc);
const char *IMG_GetError(void);
int IMG_SavePNG(SDL_Surface *surface, const char *file);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2_SDL_IMAGE_H */
