/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- display present backend
 * (CONFIG_RETRO_PRESENT_DISPLAY).
 *
 * Pushes the core's XRGB8888 frame at the Zephyr display API. On
 * rpi_zero_2w that is bcm2835_fb with a virtual framebuffer whose
 * geometry the board overlay picks for 64-byte row alignment (the
 * FB driver's contiguous-DMA fast path); the VideoCore HVS scales to
 * the monitor's native mode -- the proven Doom / SDLPoP / Mini vMac /
 * sokoban path.
 *
 * Core geometry is runtime-declared and rarely alignment-friendly
 * (2048 is 376 wide = 1504 B/row), so the backbuffer is allocated at
 * the DISPLAY's resolution and the core frame is centered into it;
 * the margins stay black.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/bcm2835_fb.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(retro_present, CONFIG_RETRO_LOG_LEVEL);

static const struct device *display_dev;
static uint32_t *backbuf;
static size_t backbuf_px;
static int disp_w;
static int disp_h;
static int core_w;
static int core_h;
static int off_x;
static int off_y;

int s2s_present_init(int width, int height)
{
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("present: display device not ready -- HDMI not connected?");
		return -ENODEV;
	}

	/* Size the virtual framebuffer to this core so the VideoCore HVS
	 * upscales it to fill the screen -- the same trick the standalone
	 * app ports use. If the driver can't resize (non-bcm2835 display, VC
	 * error) the FB keeps its boot geometry and the core is letterboxed
	 * into it, centered, as before.
	 */
	(void)bcm2835_fb_set_render_size(display_dev, (uint16_t)width,
					 (uint16_t)height);

	struct display_capabilities caps;

	display_get_capabilities(display_dev, &caps);

	disp_w = caps.x_resolution;
	disp_h = caps.y_resolution;

	if (width > disp_w || height > disp_h) {
		LOG_ERR("present: core %dx%d exceeds display %dx%d "
			"(bump render-width/render-height in the overlay)",
			width, height, disp_w, disp_h);
		return -EINVAL;
	}

	/* Backbuffer tracks the current FB geometry; grow it across core
	 * switches (a bigger core after a smaller one).
	 */
	size_t need = (size_t)disp_w * disp_h;

	if (backbuf_px < need) {
		uint32_t *p = realloc(backbuf, need * 4);

		if (!p) {
			LOG_ERR("present: backbuffer alloc failed (%dx%d)",
				disp_w, disp_h);
			return -ENOMEM;
		}
		backbuf = p;
		backbuf_px = need;
	}

	/* Opaque black margins (only visible when the FB couldn't be
	 * resized to the core and the frame is letterboxed).
	 */
	for (int i = 0; i < disp_w * disp_h; i++) {
		backbuf[i] = 0xFF000000u;
	}

	core_w = width;
	core_h = height;
	off_x = (disp_w - width) / 2;
	off_y = (disp_h - height) / 2;

	LOG_INF("present: display %dx%d (HVS virt) fmt=0x%x, core %dx%d at +%d+%d",
		disp_w, disp_h, (unsigned int)caps.current_pixel_format,
		core_w, core_h, off_x, off_y);
	return 0;
}

void s2s_present_frame(const void *xrgb_pixels, int pitch)
{
	if (!backbuf) {
		return;
	}

	/* The core's X byte is undefined; the HVS composites alpha, so
	 * force it opaque (fleet-standard).
	 */
	const uint8_t *src_row = xrgb_pixels;

	for (int y = 0; y < core_h; y++) {
		const uint32_t *px = (const uint32_t *)src_row;
		uint32_t *dst = backbuf + (size_t)(y + off_y) * disp_w + off_x;

		for (int x = 0; x < core_w; x++) {
			dst[x] = 0xFF000000u | (px[x] & 0x00FFFFFFu);
		}
		src_row += pitch;
	}

	struct display_buffer_descriptor desc = {
		.buf_size = (size_t)disp_w * disp_h * 4,
		.width = disp_w,
		.height = disp_h,
		.pitch = disp_w,
		.frame_incomplete = false,
	};

	int rc = display_write(display_dev, 0, 0, &desc, backbuf);

	if (rc != 0) {
		LOG_ERR("present: display_write rc=%d", rc);
	}
}
