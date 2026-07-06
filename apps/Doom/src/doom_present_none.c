/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- "none" present backend (CONFIG_DOOM_PRESENT_NONE).
 * Frames are dropped; a periodic counter on the log confirms the game
 * is presenting. Link/sim bring-up before the capture or display
 * backend is wired.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(doom_present, CONFIG_SDL2SHIM_LOG_LEVEL);

static uint32_t frames;

int s2s_present_init(int width, int height)
{
	LOG_INF("present: none backend (%dx%d frames dropped)", width, height);
	return 0;
}

void s2s_present_frame(const void *xrgb_pixels, int pitch)
{
	ARG_UNUSED(xrgb_pixels);
	ARG_UNUSED(pitch);

	frames++;
	if ((frames % 64) == 0) {
		LOG_INF("present: %u frames", frames);
	}
}
