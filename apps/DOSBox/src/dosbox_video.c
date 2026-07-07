/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa DOSBox-X -- shim window-surface implementation.
 *
 * DOSBox-X's output=surface path draws into SDL_GetWindowSurface() and
 * presents with SDL_UpdateWindowSurface(Rects). One fixed window whose
 * surface tracks the emulated mode's size (text 720x400, VGA 320x200,
 * VESA up to 1024 wide); the present seam (s2s_present_*) receives
 * whole frames and the HVS (hardware) or semihost capture (qemu) deals
 * with scaling/inspection.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(dbx_video, CONFIG_SDL2SHIM_LOG_LEVEL);

/* SDL_CreateWindow comes from the shim lib (shim_core.c); this side
 * only tracks the emulated mode's dimensions for the surface.
 */
static struct {
	int w, h;
} the_window;

static SDL_Surface *win_surface;
static SDL_PixelFormat win_format;
static bool present_up;

/* called by the embedded sdlmain on every mode change */
void dbx_window_resize(int w, int h)
{
	if (w == the_window.w && h == the_window.h && win_surface != NULL) {
		return;
	}
	the_window.w = w;
	the_window.h = h;

	if (win_surface != NULL) {
		free(win_surface->pixels);
		free(win_surface);
		win_surface = NULL;
	}

	int rc = s2s_present_init(w, h);

	present_up = (rc == 0);
	if (!present_up) {
		LOG_WRN("present init %dx%d failed (%d); frames drop", w, h, rc);
	}
}

SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
	if (window == NULL || the_window.w <= 0 || the_window.h <= 0) {
		return NULL;
	}
	if (win_surface != NULL &&
	    win_surface->w == the_window.w && win_surface->h == the_window.h) {
		return win_surface;
	}
	if (win_surface != NULL) {
		free(win_surface->pixels);
		free(win_surface);
	}

	win_format.format = SDL_PIXELFORMAT_RGB888; /* 32bpp XRGB */
	win_format.palette = NULL;
	win_format.BitsPerPixel = 32;
	win_format.BytesPerPixel = 4;
	win_format.Rmask = 0x00FF0000u; win_format.Rshift = 16;
	win_format.Gmask = 0x0000FF00u; win_format.Gshift = 8;
	win_format.Bmask = 0x000000FFu; win_format.Bshift = 0;
	win_format.Amask = 0; win_format.Ashift = 24;

	win_surface = calloc(1, sizeof(*win_surface));
	if (win_surface == NULL) {
		return NULL;
	}
	win_surface->format = &win_format;
	win_surface->w = the_window.w;
	win_surface->h = the_window.h;
	win_surface->pitch = the_window.w * 4;
	win_surface->pixels = calloc((size_t)win_surface->pitch, (size_t)the_window.h);
	if (win_surface->pixels == NULL) {
		free(win_surface);
		win_surface = NULL;
		return NULL;
	}
	win_surface->clip_rect.w = the_window.w;
	win_surface->clip_rect.h = the_window.h;
	LOG_INF("window surface %dx%d (32bpp XRGB)", the_window.w, the_window.h);
	return win_surface;
}

int SDL_UpdateWindowSurface(SDL_Window *window)
{
	ARG_UNUSED(window);
	if (win_surface == NULL || !present_up) {
		return -1;
	}
	s2s_present_frame(win_surface->pixels, win_surface->pitch);
	return 0;
}

int SDL_UpdateWindowSurfaceRects(SDL_Window *window, const SDL_Rect *rects, int numrects)
{
	/* the present seam takes whole frames; dirty rects are advisory */
	ARG_UNUSED(rects); ARG_UNUSED(numrects);
	return SDL_UpdateWindowSurface(window);
}
