/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban -- frame-dropping present backend
 * (CONFIG_SOKOBAN_PRESENT_NONE, link/bring-up).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(sok_present, CONFIG_SDL2SHIM_LOG_LEVEL);

int s2s_present_init(int width, int height)
{
	LOG_INF("present: none backend (%dx%d frames dropped)", width, height);
	return 0;
}

void s2s_present_frame(const void *pixels, int pitch)
{
	ARG_UNUSED(pixels);
	ARG_UNUSED(pitch);
}
