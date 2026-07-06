/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- display present backend (CONFIG_DOOM_PRESENT_DISPLAY).
 *
 * Pushes Doom's 320x200 XRGB8888 frame at the Zephyr display API. On
 * rpi_zero_2w that is bcm2835_fb with a 320x200 virtual framebuffer
 * (320 * 4 = 1280 B/row, 64-byte aligned so the driver's contiguous-DMA
 * fast path engages); the VideoCore HVS scales 320x200 to the monitor's
 * native mode -- the same proven path as the Jet, wipeout and SDLPoP
 * ports.
 *
 * Doom's pixels are already 0x00RRGGBB; the scanout wants 0xFFRRGGBB,
 * so the only conversion is forcing the alpha/x byte opaque.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(doom_present, CONFIG_SDL2SHIM_LOG_LEVEL);

#define DOOM_W 320
#define DOOM_H 200

static const struct device *display_dev;
static uint32_t backbuf[DOOM_W * DOOM_H];
static bool ready;

int s2s_present_init(int width, int height)
{
	if (width != DOOM_W || height != DOOM_H) {
		LOG_ERR("present: %dx%d requested, backend is fixed %dx%d",
			width, height, DOOM_W, DOOM_H);
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

	if (caps.x_resolution != DOOM_W || caps.y_resolution != DOOM_H) {
		LOG_WRN("present: display reports %ux%u, expected %ux%u "
			"(check render-width/render-height in overlay)",
			caps.x_resolution, caps.y_resolution, DOOM_W, DOOM_H);
	}

	ready = true;
	return 0;
}

void s2s_present_frame(const void *xrgb_pixels, int pitch)
{
	if (!ready) {
		return;
	}

	const uint8_t *src_row = xrgb_pixels;
	uint32_t *dst = backbuf;

	for (int y = 0; y < DOOM_H; y++) {
		const uint32_t *px = (const uint32_t *)src_row;

		for (int x = 0; x < DOOM_W; x++) {
			*dst++ = 0xFF000000u | (px[x] & 0x00FFFFFFu);
		}
		src_row += pitch;
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(backbuf),
		.width = DOOM_W,
		.height = DOOM_H,
		.pitch = DOOM_W,
		.frame_incomplete = false,
	};

	int rc = display_write(display_dev, 0, 0, &desc, backbuf);

	if (rc != 0) {
		LOG_ERR("present: display_write rc=%d", rc);
	}
}
