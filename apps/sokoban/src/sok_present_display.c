/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban -- display present backend (CONFIG_SOKOBAN_PRESENT_DISPLAY).
 *
 * Pushes the renderer's 640x480 ARGB8888 backing at the Zephyr display
 * API. On rpi_zero_2w that is bcm2835_fb with a 640x480 virtual
 * framebuffer (640 * 4 = 2560 B/row, 64-byte aligned so the driver's
 * contiguous-DMA fast path engages); the VideoCore HVS scales to the
 * monitor's native mode -- the proven Doom / SDLPoP / Mini vMac path.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(sok_present, CONFIG_SDL2SHIM_LOG_LEVEL);

#define SOK_W 640
#define SOK_H 480

static const struct device *display_dev;
static uint32_t backbuf[SOK_W * SOK_H];
static bool ready;

int s2s_present_init(int width, int height)
{
	if (width != SOK_W || height != SOK_H) {
		LOG_ERR("present: %dx%d requested, backend is fixed %dx%d",
			width, height, SOK_W, SOK_H);
		return -EINVAL;
	}

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("present: display device not ready -- HDMI not connected?");
		return -ENODEV;
	}

	struct display_capabilities caps;

	display_get_capabilities(display_dev, &caps);
	LOG_INF("present: display %ux%u (HVS virt) fmt=0x%x",
		caps.x_resolution, caps.y_resolution,
		(unsigned int)caps.current_pixel_format);

	if (caps.x_resolution != SOK_W || caps.y_resolution != SOK_H) {
		LOG_WRN("present: display reports %ux%u, expected %ux%u "
			"(check render-width/render-height in overlay)",
			caps.x_resolution, caps.y_resolution, SOK_W, SOK_H);
	}

	ready = true;
	return 0;
}

void s2s_present_frame(const void *argb_pixels, int pitch)
{
	if (!ready) {
		return;
	}

	const uint8_t *src_row = argb_pixels;
	uint32_t *dst = backbuf;

	for (int y = 0; y < SOK_H; y++) {
		const uint32_t *px = (const uint32_t *)src_row;

		for (int x = 0; x < SOK_W; x++) {
			*dst++ = 0xFF000000u | (px[x] & 0x00FFFFFFu);
		}
		src_row += pitch;
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(backbuf),
		.width = SOK_W,
		.height = SOK_H,
		.pitch = SOK_W,
		.frame_incomplete = false,
	};

	int rc = display_write(display_dev, 0, 0, &desc, backbuf);

	if (rc != 0) {
		LOG_ERR("present: display_write rc=%d", rc);
	}
}
