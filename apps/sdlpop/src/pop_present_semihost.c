/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- semihosting capture present backend
 * (CONFIG_SDLPOP_PRESENT_SEMIHOST, qemu sim targets).
 *
 * Frames land on the host filesystem as binary PPM (P6) files --
 * the work-order's "fb PNG dump" gate, one `magick` away from PNG.
 * The game's RGB24 buffer is already [R,G,B] bytes, so the dump is
 * header + rows, no conversion.
 *
 * Frame 1 is always written; with CONFIG_SDLPOP_CAPTURE_EVERY=N > 0
 * every Nth frame is written too (phase-4 frame sequences).
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/common/semihost.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(pop_present, CONFIG_SDLPOP_LOG_LEVEL);

static int fb_w;
static int fb_h;
static uint32_t frames;

int s2s_present_init(int width, int height)
{
	fb_w = width;
	fb_h = height;
	LOG_INF("present: semihost capture backend (%dx%d)", width, height);
	return 0;
}

void s2s_present_frame(const void *rgb24_pixels, int pitch)
{
	frames++;

	bool capture = (frames == 1);

	if (CONFIG_SDLPOP_CAPTURE_EVERY > 0 &&
	    (frames % CONFIG_SDLPOP_CAPTURE_EVERY) == 0) {
		capture = true;
	}
	if (!capture) {
		return;
	}

	char name[48];
	char header[32];

	snprintf(name, sizeof(name), "sdlpop_frame_%06u.ppm", frames);

	long fd = semihost_open(name, SEMIHOST_OPEN_WB);

	if (fd < 0) {
		LOG_ERR("present: semihost_open(%s) = %ld", name, fd);
		return;
	}

	int hlen = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", fb_w, fb_h);

	semihost_write(fd, header, hlen);

	const uint8_t *row = rgb24_pixels;

	for (int y = 0; y < fb_h; y++) {
		semihost_write(fd, row, (long)fb_w * 3);
		row += pitch;
	}
	semihost_close(fd);
	LOG_INF("present: wrote %s", name);
}
