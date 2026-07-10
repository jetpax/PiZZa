/* Compat SDL_config.h — the bundled SDL1 cdrom layer (vs/sdl/src/cdrom,
 * force-included by dos/cdrom.cpp) expects SDL1's config header. On this
 * platform the ladder lands on the dummy backend; libc basics suffice.
 */
#ifndef PIZZA_COMPAT_SDL_CONFIG_H
#define PIZZA_COMPAT_SDL_CONFIG_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STDIO_H 1

#ifdef __cplusplus
extern "C" int SDL_SetError(const char *fmt, ...);
#else
extern int SDL_SetError(const char *fmt, ...);
#endif
#ifndef SDL_OutOfMemory
#define SDL_OutOfMemory() SDL_SetError("Out of memory")
#endif

#endif
