/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa DOSBox-X -- semihosting capture present backend
 * (CONFIG_DOSBOX_PRESENT_SEMIHOST, qemu sim gates). Doom-port pattern:
 * frames land on the host as binary PPM (P6); pixels are 32bpp XRGB.
 *
 * Frame 1 is always written; every CONFIG_DOSBOX_CAPTURE_EVERY'th
 * frame after that. Mode changes reset the counter so the first frame
 * of each new mode is captured too.
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/common/semihost.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(dbx_present, CONFIG_SDL2SHIM_LOG_LEVEL);

#define MAX_W 1280

static int fb_w;
static int fb_h;
static uint32_t frames;
static uint32_t captures;

int s2s_present_init(int width, int height)
{
	if (width > MAX_W) {
		return -1;
	}
	fb_w = width;
	fb_h = height;
	frames = 0;
	LOG_INF("present: semihost capture backend (%dx%d)", width, height);
	return 0;
}

void s2s_present_frame(const void *xrgb_pixels, int pitch)
{
	frames++;

	bool capture = (frames == 1);

	if (CONFIG_DOSBOX_CAPTURE_EVERY > 0 &&
	    (frames % CONFIG_DOSBOX_CAPTURE_EVERY) == 0) {
		capture = true;
	}
	if (!capture) {
		return;
	}

	char name[48];
	char header[32];

	snprintf(name, sizeof(name), "dosbox_frame_%06u.ppm", ++captures);

	long fd = semihost_open(name, SEMIHOST_OPEN_WB);

	if (fd < 0) {
		LOG_ERR("present: semihost_open(%s) = %ld", name, fd);
		return;
	}

	int hlen = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", fb_w, fb_h);

	semihost_write(fd, header, hlen);

	static uint8_t rowbuf[MAX_W * 3];
	const uint8_t *row = xrgb_pixels;

	for (int y = 0; y < fb_h; y++) {
		const uint32_t *px = (const uint32_t *)row;
		uint8_t *o = rowbuf;

		for (int x = 0; x < fb_w; x++) {
			uint32_t p = px[x];

			*o++ = (uint8_t)(p >> 16);
			*o++ = (uint8_t)(p >> 8);
			*o++ = (uint8_t)(p);
		}
		semihost_write(fd, rowbuf, (long)fb_w * 3);
		row += pitch;
	}
	semihost_close(fd);
	LOG_INF("present: wrote %s (frame %u, %dx%d)", name, frames, fb_w, fb_h);
}
