/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- "none" present backend (CONFIG_MINIVMAC_PRESENT_NONE).
 * Frames are dropped; a periodic counter confirms the emulator is
 * presenting. Link/sim bring-up before the capture or display backend.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(minivmac_present, CONFIG_SDL2SHIM_LOG_LEVEL);

static uint32_t frames;

int s2s_present_init(int width, int height)
{
	LOG_INF("present: none backend (%dx%d frames dropped)", width, height);
	return 0;
}

void s2s_present_frame(const void *argb_pixels, int pitch)
{
	ARG_UNUSED(argb_pixels);
	ARG_UNUSED(pitch);

	frames++;
	if ((frames % 64) == 0) {
		LOG_INF("present: %u frames", frames);
	}
}
