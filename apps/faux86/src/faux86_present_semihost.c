/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PiZZa Faux86 -- semihosting capture present backend
 * (CONFIG_FAUX86_PRESENT_SEMIHOST, qemu sim gate). DOSBox-port pattern:
 * frames land on the host as binary PPM (P6); pixels are 32bpp XRGB.
 *
 * Frame 1 is always written; every CONFIG_FAUX86_CAPTURE_EVERY'th frame
 * after that. A mode change (new width/height) resets the counter so the
 * first frame of each new mode is captured too.
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/common/semihost.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>

#include "faux86_present.h"

#define MAX_W 1280

static int fb_w;
static int fb_h;
static uint32_t frames;
static uint32_t captures;

int s2s_present_init(int width, int height)
{
	if (width > MAX_W) {
		printk("[faux86] present: width %d > MAX_W %d\n", width, MAX_W);
		return -1;
	}
	fb_w = width;
	fb_h = height;
	frames = 0;
	printk("[faux86] present: semihost capture backend (%dx%d)\n", width, height);
	return 0;
}

void s2s_present_frame(const void *xrgb_pixels, int pitch)
{
	frames++;

	/*
	 * Only capture frames with drawn content -- skip the all-black cleared
	 * screen the BIOS shows during early POST (under slow TCG that is most
	 * of them). If nothing is ever non-black, zero PPMs get written, which
	 * cleanly distinguishes a real render bug from a slow boot.
	 */
	const uint8_t *base = xrgb_pixels;
	bool has_content = false;

	for (int y = 0; y < fb_h && !has_content; y++) {
		const uint32_t *px = (const uint32_t *)(base + (size_t)pitch * y);

		for (int x = 0; x < fb_w; x++) {
			if (px[x] & 0x00FFFFFFu) {
				has_content = true;
				break;
			}
		}
	}
	if (!has_content) {
		if ((frames % 500) == 0) {
			printk("[faux86] present: frame %u still blank\n", frames);
		}
		return;
	}

	/* throttle content captures so a busy screen doesn't flood the host */
	if (captures > 0 && CONFIG_FAUX86_CAPTURE_EVERY > 0 &&
	    (frames % CONFIG_FAUX86_CAPTURE_EVERY) != 0) {
		return;
	}

	char name[48];
	char header[32];

	snprintf(name, sizeof(name), "faux86_frame_%06u.ppm", ++captures);

	long fd = semihost_open(name, SEMIHOST_OPEN_WB);

	if (fd < 0) {
		printk("[faux86] present: semihost_open(%s) = %ld\n", name, fd);
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
	printk("[faux86] present: wrote %s (frame %u, %dx%d)\n", name, frames, fb_w, fb_h);
}
