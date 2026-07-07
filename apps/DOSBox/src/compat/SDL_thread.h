/* Compat SDL_thread.h — SDL_sound.c includes it; the shim is
 * single-threaded, mutex API lives in SDL.h.
 */
#ifndef PIZZA_COMPAT_SDL_THREAD_H
#define PIZZA_COMPAT_SDL_THREAD_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_WaitThread(SDL_Thread *thread, int *status);

#ifdef __cplusplus
}
#endif

#endif
