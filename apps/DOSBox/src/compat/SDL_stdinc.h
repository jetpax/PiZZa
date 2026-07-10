/* Compat SDL_stdinc.h — DOSBox-X's decoders include this directly.
 * The shim's monolithic SDL.h carries the types; this adds the SDL_*
 * stdlib aliases and the one RW call the shim header lacks.
 */
#ifndef PIZZA_COMPAT_SDL_STDINC_H
#define PIZZA_COMPAT_SDL_STDINC_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "SDL.h"

#ifndef SDL_memcpy
#define SDL_memcpy  memcpy
#endif
#ifndef SDL_memcmp
#define SDL_memcmp  memcmp
#endif
#ifndef SDL_memmove
#define SDL_memmove memmove
#endif
#ifndef SDL_realloc
#define SDL_realloc realloc
#endif
#ifndef SDL_calloc
#define SDL_calloc  calloc
#endif
#ifndef SDL_qsort
#define SDL_qsort   qsort
#endif
#ifndef SDL_strlen
#define SDL_strlen  strlen
#endif
#ifndef SDL_strcmp
#define SDL_strcmp  strcmp
#endif

#ifndef RW_SEEK_SET
#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2
#endif

#ifdef __cplusplus
extern "C" {
#endif
static inline Sint64 SDL_RWseek(SDL_RWops *ctx, Sint64 offset, int whence)
{
	return ctx->seek(ctx, offset, whence);
}
#ifdef __cplusplus
}
#endif

#endif
