/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sdl2shim-over-libretro -- per-core descriptor.
 *
 * A core build (game + shim groups + SDL2SHIM_LIBRETRO_SOURCES) adds
 * one small TU defining s2s_libretro_desc: identity, geometry, timing
 * and the retropad -> SDL-scancode keymap the glue edge-converts into
 * the core's own shim event queue. The game itself stays untouched.
 */

#ifndef PIZZA_SDL2SHIM_LIBRETRO_H
#define PIZZA_SDL2SHIM_LIBRETRO_H

#include <stddef.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct s2s_libretro_keybind {
	uint8_t retro_id;     /* RETRO_DEVICE_ID_JOYPAD_* (0..15) */
	uint16_t scancode;    /* SDL_SCANCODE_* the game reads */
};

struct s2s_libretro_desc {
	const char *name;
	const char *version;
	int width;            /* the game's native present geometry */
	int height;
	double fps;           /* retro_run pacing the frontend applies */
	double sample_rate;   /* 44100 for audio cores, 0 = silent */
	const struct s2s_libretro_keybind *keymap;
	size_t keymap_len;
};

/* Defined by the per-core descriptor TU. */
extern const struct s2s_libretro_desc s2s_libretro_desc;

/* Audio pump: called by the glue once per retro_run; pulls one video
 * frame's worth of samples from the game's SDL audio callback and
 * hands them to the frontend. Implemented in shim_audio_libretro.c.
 */
size_t s2s_audio_lr_pump(size_t (*batch)(const int16_t *, size_t),
			 double fps);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2SHIM_LIBRETRO_H */
