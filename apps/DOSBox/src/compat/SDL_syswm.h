/* Compat SDL_syswm.h — window-manager info is meaningless on the shim;
 * menu.cpp/sdl_mapper.cpp include it for their Windows/macOS paths only.
 */
#ifndef PIZZA_COMPAT_SDL_SYSWM_H
#define PIZZA_COMPAT_SDL_SYSWM_H

#include "SDL.h"

typedef struct SDL_SysWMinfo {
	SDL_version version;
	int subsystem;
} SDL_SysWMinfo;

typedef struct SDL_SysWMmsg {
	SDL_version version;
	int subsystem;
} SDL_SysWMmsg;

#endif
