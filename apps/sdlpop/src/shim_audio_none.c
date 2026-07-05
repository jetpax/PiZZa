/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- "none" audio backend (CONFIG_SDLPOP_AUDIO_NONE).
 *
 * open() fails, so SDLPoP sets digi_unavailable and skips its entire
 * audio subsystem. The I2S backend (work-order §5a) replaces this
 * file at build time via Kconfig and implements the same six
 * functions for real: open spawns the feeder thread servicing the
 * game's callback into a double-buffered DMA ring.
 */

#include <zephyr/kernel.h>

#include "pop_shim.h"

int pop_audio_open(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	ARG_UNUSED(desired);
	ARG_UNUSED(obtained);
	pop_set_error("audio: no backend (CONFIG_SDLPOP_AUDIO_NONE)");
	return -1;
}

void pop_audio_close(void)
{
}

void pop_audio_pause(int pause_on)
{
	ARG_UNUSED(pause_on);
}

void pop_audio_lock(void)
{
}

void pop_audio_unlock(void)
{
}

SDL_AudioStatus pop_audio_status(void)
{
	return SDL_AUDIO_STOPPED;
}
