/* SPDX-License-Identifier: GPL-2.0-or-later */
/* PiZZa Faux86 -- discard frames (CONFIG_FAUX86_PRESENT_NONE). */

#include "faux86_present.h"

int s2s_present_init(int width, int height)
{
	(void)width;
	(void)height;
	return 0;
}

void s2s_present_frame(const void *xrgb_pixels, int pitch)
{
	(void)xrgb_pixels;
	(void)pitch;
}
