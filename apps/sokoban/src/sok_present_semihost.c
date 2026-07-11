/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban -- semihosting capture present backend
 * (CONFIG_SOKOBAN_PRESENT_SEMIHOST, qemu sim gates).
 *
 * Frames land on the host filesystem as binary PPM (P6) files. Frame 1
 * is always written; with CONFIG_SOKOBAN_CAPTURE_EVERY=N > 0 every Nth
 * frame is written too, so the scripted title -> level 1 -> movement
 * progression is captured across several files.
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/common/semihost.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(sok_present, CONFIG_SDL2SHIM_LOG_LEVEL);

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

void s2s_present_frame(const void *argb_pixels, int pitch)
{
	frames++;

	bool capture = (frames == 1);

	if (CONFIG_SOKOBAN_CAPTURE_EVERY > 0 &&
	    (frames % CONFIG_SOKOBAN_CAPTURE_EVERY) == 0) {
		capture = true;
	}
	if (!capture) {
		return;
	}

	char name[48];
	char header[32];

	snprintf(name, sizeof(name), "sokoban_frame_%06u.ppm", frames);

	long fd = semihost_open(name, SEMIHOST_OPEN_WB);

	if (fd < 0) {
		LOG_ERR("present: semihost_open(%s) = %ld", name, fd);
		return;
	}

	int hlen = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", fb_w, fb_h);

	semihost_write(fd, header, hlen);

	/* Unpack ARGB -> RGB one row at a time. */
	static uint8_t rowbuf[640 * 3];
	const uint8_t *row = argb_pixels;

	for (int y = 0; y < fb_h; y++) {
		const uint32_t *px = (const uint32_t *)row;
		uint8_t *o = rowbuf;

		for (int x = 0; x < fb_w; x++) {
			uint32_t p = px[x];

			*o++ = (uint8_t)(p >> 16);  /* R */
			*o++ = (uint8_t)(p >> 8);   /* G */
			*o++ = (uint8_t)(p);        /* B */
		}
		semihost_write(fd, rowbuf, (long)fb_w * 3);
		row += pitch;
	}
	semihost_close(fd);
	LOG_INF("present: wrote %s", name);
}
