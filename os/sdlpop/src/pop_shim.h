/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- internal interfaces between the shim_*.c files
 * and the platform seams (present path, audio backend, event
 * producers). Nothing in here is visible to SDLPoP.
 */

#ifndef PIZZA_POP_SHIM_H
#define PIZZA_POP_SHIM_H

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── error (shim_core.c) ─────────────────────────────────────── */

void pop_set_error(const char *fmt, ...);

/* ── surface internals (shim_surface.c) ──────────────────────── */

/* Every SDL_Surface handed to the game is the first member of a
 * pop_surface, so shim code can downcast to reach blit state the
 * public struct doesn't carry.
 */
struct pop_surface {
	SDL_Surface s;          /* must stay first */
	SDL_PixelFormat fmt;
	SDL_Palette pal;
	SDL_Color colors[256];
	Uint32 colorkey;
	int has_colorkey;
	Uint8 alpha_mod;
	SDL_BlendMode blend;
};

#define POP_SURF(sdl_surface) ((struct pop_surface *)(sdl_surface))

/* ── event queue (shim_event.c) ──────────────────────────────── */

/* Producer seam: any source (scripted input, timer callbacks, HOGP
 * keyboard thread later) submits through here. ISR-safe.
 * KEYDOWN/KEYUP submissions also update the SDL_GetKeyboardState
 * array. Returns 0 on success, -1 if the queue is full.
 */
int pop_event_submit(const SDL_Event *event);

/* Scripted synthetic input source (CONFIG_SDLPOP_SCRIPTED_INPUT). */
void pop_scripted_input_start(void);

/* ── present seam (pop_present_*.c, Kconfig-selected) ────────── */

/* The video shim composes the final 320x200 frame and pushes it
 * through this seam. Backends: bcm2835_fb display path (hardware),
 * capture/none (sim). pixels is the game's RGB24 buffer, memory
 * byte order R,G,B (LE masks per SDLPoP types.h).
 */
int pop_present_init(int width, int height);
void pop_present_frame(const void *rgb24_pixels, int pitch);

/* Video-path self-test (CONFIG_SDLPOP_VIDEO_SELFTEST): color bars
 * through the full surface->texture->present path before the game
 * starts.
 */
void pop_video_selftest(void);

/* ── embedded asset store (pop_assets.c, work-order §4) ──────── */

/* Direct pack lookup for shim-internal users (IMG_Load, selftests).
 * Returns a pointer into the read-only pack, or NULL.
 */
const void *pop_asset_find(const char *path, size_t *size);

/* ── audio backend seam (§5a -- link-time function table) ────── */

/* SDL audio entry points in shim_stub.c delegate to these. The
 * backend is chosen by Kconfig at build time (no runtime vtable):
 * shim_audio_none.c today, shim_audio_i2s.c when §5a lands.
 */
int pop_audio_open(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void pop_audio_close(void);
void pop_audio_pause(int pause_on);
void pop_audio_lock(void);
void pop_audio_unlock(void);
SDL_AudioStatus pop_audio_status(void);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_POP_SHIM_H */
