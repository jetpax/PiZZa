/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- null present backend
 * (CONFIG_RETRO_PRESENT_NONE, link/sim bring-up).
 */

#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(retro_present, CONFIG_RETRO_LOG_LEVEL);

int s2s_present_init(int width, int height)
{
	LOG_INF("present: null backend (%dx%d)", width, height);
	return 0;
}

void s2s_present_frame(const void *pixels, int pitch)
{
	(void)pixels;
	(void)pitch;
}
