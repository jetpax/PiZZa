/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- internal interfaces between the shim_*.c files
 * and the platform seams (present path, audio backend, event
 * producers). Nothing in here is visible to SDLPoP.
 */

#ifndef PIZZA_SDL2SHIM_H
#define PIZZA_SDL2SHIM_H

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── error (shim_core.c) ─────────────────────────────────────── */

void s2s_set_error(const char *fmt, ...);

/* ── surface internals (shim_surface.c) ──────────────────────── */

/* Every SDL_Surface handed to the game is the first member of a
 * s2s_surface, so shim code can downcast to reach blit state the
 * public struct doesn't carry.
 */
struct s2s_surface {
	SDL_Surface s;          /* must stay first */
	SDL_PixelFormat fmt;
	SDL_Palette pal;
	SDL_Color colors[256];
	Uint32 colorkey;
	int has_colorkey;
	Uint8 alpha_mod;
	SDL_BlendMode blend;
};

#define S2S_SURF(sdl_surface) ((struct s2s_surface *)(sdl_surface))

/* ── event queue (shim_event.c) ──────────────────────────────── */

/* Producer seam: any source (scripted input, timer callbacks, HOGP
 * keyboard thread later) submits through here. ISR-safe.
 * KEYDOWN/KEYUP submissions also update the SDL_GetKeyboardState
 * array. Returns 0 on success, -1 if the queue is full.
 */
int s2s_event_submit(const SDL_Event *event);

/* Scripted synthetic input source. Each consuming app supplies its own
 * game-specific timeline (Doom's doom_scripted.c, SDLPoP's shim_scripted.c)
 * behind this seam; the lib declares it but does not implement it.
 */
void s2s_scripted_input_start(void);

/* ── present seam (s2s_present_*.c, Kconfig-selected) ────────── */

/* The video shim composes the final 320x200 frame and pushes it
 * through this seam. Backends: bcm2835_fb display path (hardware),
 * capture/none (sim). pixels is the game's RGB24 buffer, memory
 * byte order R,G,B (LE masks per SDLPoP types.h).
 */
int s2s_present_init(int width, int height);
void s2s_present_frame(const void *rgb24_pixels, int pitch);

/* Video-path self-test (CONFIG_SDL2SHIM_VIDEO_SELFTEST): color bars
 * through the full surface->texture->present path before the game
 * starts.
 */
void s2s_video_selftest(void);

/* ── embedded asset store (s2s_assets.c, work-order §4) ──────── */

/* Direct pack lookup for shim-internal users (IMG_Load, selftests).
 * Returns a pointer into the read-only pack, or NULL.
 */
const void *s2s_asset_find(const char *path, size_t *size);

/* ── audio backend seam (§5a -- link-time function table) ────── */

/* SDL audio entry points in shim_stub.c delegate to these. The
 * backend is chosen by Kconfig at build time (no runtime vtable):
 * shim_audio_none.c today, shim_audio_i2s.c when §5a lands.
 */
int s2s_audio_open(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void s2s_audio_close(void);
void s2s_audio_pause(int pause_on);
void s2s_audio_lock(void);
void s2s_audio_unlock(void);
SDL_AudioStatus s2s_audio_status(void);

#ifdef CONFIG_SDL2SHIM_AUDIO_HDMI
/* Diagnostics for the `pop audio` shell command (shim_shell.c). */
struct s2s_audio_hdmi_stats {
	int opened;
	int tone_mode;
	int paused;
	int dma_chan;
	unsigned int blocks_played;
	unsigned int underruns;
	unsigned int dma_errors;
};

void s2s_audio_hdmi_get_stats(struct s2s_audio_hdmi_stats *out);

/* Skip the next N feeder wakeups (one per 4 ms block) to starve the
 * ring on demand -- the M2 underrun-recovery demonstration.
 */
void s2s_audio_hdmi_starve(unsigned int blocks);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2SHIM_H */
