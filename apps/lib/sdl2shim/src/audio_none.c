/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- "none" audio backend (CONFIG_SDL2SHIM_AUDIO_NONE).
 *
 * open() fails, so SDLPoP sets digi_unavailable and skips its entire
 * audio subsystem. The I2S backend (work-order §5a) replaces this
 * file at build time via Kconfig and implements the same six
 * functions for real: open spawns the feeder thread servicing the
 * game's callback into a double-buffered DMA ring.
 */

#include <zephyr/kernel.h>

#include "sdl2shim.h"

int s2s_audio_open(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	ARG_UNUSED(desired);
	ARG_UNUSED(obtained);
	s2s_set_error("audio: no backend (CONFIG_SDL2SHIM_AUDIO_NONE)");
	return -1;
}

void s2s_audio_close(void)
{
}

void s2s_audio_pause(int pause_on)
{
	ARG_UNUSED(pause_on);
}

void s2s_audio_lock(void)
{
}

void s2s_audio_unlock(void)
{
}

SDL_AudioStatus s2s_audio_status(void)
{
	return SDL_AUDIO_STOPPED;
}
