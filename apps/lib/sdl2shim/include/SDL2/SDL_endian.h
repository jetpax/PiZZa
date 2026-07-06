/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- minimal <SDL2/SDL_endian.h>. Doom's i_swap.h pulls
 * this for SDL_SwapLE16/32; those macros (and the endian constants)
 * live in SDL.h, so this is a thin redirect.
 */

#ifndef PIZZA_SDL2_SDL_ENDIAN_H
#define PIZZA_SDL2_SDL_ENDIAN_H

#include <SDL2/SDL.h>

#endif /* PIZZA_SDL2_SDL_ENDIAN_H */
