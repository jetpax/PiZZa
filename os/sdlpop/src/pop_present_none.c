/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- "none" present backend
 * (CONFIG_SDLPOP_PRESENT_NONE). Frames are dropped; a counter on the
 * log confirms the game is presenting. Used for link/sim targets
 * before the capture backend exists. The hardware backend
 * (pop_present_display.c) pushes frames at the bcm2835_fb driver and
 * lets the HVS scale 320x200 to the panel.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pop_shim.h"

LOG_MODULE_REGISTER(pop_present, CONFIG_SDLPOP_LOG_LEVEL);

static uint32_t frames;

int pop_present_init(int width, int height)
{
	LOG_INF("present: none backend (%dx%d frames dropped)", width, height);
	return 0;
}

void pop_present_frame(const void *rgb24_pixels, int pitch)
{
	ARG_UNUSED(rgb24_pixels);
	ARG_UNUSED(pitch);

	frames++;
	if ((frames % 64) == 0) {
		LOG_INF("present: %u frames", frames);
	}
}
