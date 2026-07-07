/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa DOSBox-X -- discard present backend (CONFIG_DOSBOX_PRESENT_NONE).
 */

#include <zephyr/kernel.h>

#include "sdl2shim.h"

int s2s_present_init(int width, int height)
{
	ARG_UNUSED(width);
	ARG_UNUSED(height);
	return 0;
}

void s2s_present_frame(const void *pixels, int pitch)
{
	ARG_UNUSED(pixels);
	ARG_UNUSED(pitch);
}
