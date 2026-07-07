/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- display present backend
 * (CONFIG_SDLPOP_PRESENT_DISPLAY).
 *
 * Pushes the game's composed 320x200 RGB24 frame at the Zephyr
 * display API. On rpi_zero_2w that is the bcm2835_fb driver with a
 * 320x200 virtual framebuffer: 320 * 4 = 1280 bytes/row is 64-byte
 * aligned so the driver's contiguous-DMA fast path engages, and the
 * VideoCore HVS scales to the monitor's native mode -- the same
 * proven path as the Jet and wipeout ports.
 *
 * Conversion: game memory bytes [R,G,B] (SDLPoP's LE 24bpp masks)
 * -> LE ARGB8888 words (A<<24 | R<<16 | G<<8 | B), matching what the
 * wipeout port established the VC scanout expects.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(pop_present, CONFIG_SDLPOP_LOG_LEVEL);

#define POP_W 320
#define POP_H 200

static const struct device *display_dev;
static uint32_t backbuf[POP_W * POP_H];
static bool ready;

int s2s_present_init(int width, int height)
{
	if (width != POP_W || height != POP_H) {
		LOG_ERR("present: %dx%d requested, backend is fixed %dx%d",
			width, height, POP_W, POP_H);
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

	if (caps.x_resolution != POP_W || caps.y_resolution != POP_H) {
		LOG_WRN("present: display reports %ux%u, expected %ux%u "
			"(check render-width/render-height in overlay)",
			caps.x_resolution, caps.y_resolution, POP_W, POP_H);
	}

	ready = true;
	return 0;
}

void s2s_present_frame(const void *rgb24_pixels, int pitch)
{
	if (!ready) {
		return;
	}

	const uint8_t *src_row = rgb24_pixels;
	uint32_t *dst = backbuf;

	for (int y = 0; y < POP_H; y++) {
		const uint8_t *px = src_row;

		for (int x = 0; x < POP_W; x++) {
			*dst++ = 0xFF000000u |
				 ((uint32_t)px[0] << 16) |
				 ((uint32_t)px[1] << 8) |
				 (uint32_t)px[2];
			px += 3;
		}
		src_row += pitch;
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(backbuf),
		.width = POP_W,
		.height = POP_H,
		.pitch = POP_W,
		.frame_incomplete = false,
	};

	/* Synchronous write: single backbuffer, same reasoning as the
	 * wipeout port (an async kick from frame N would race frame
	 * N+1's composition).
	 */
	int rc = display_write(display_dev, 0, 0, &desc, backbuf);

	if (rc != 0) {
		LOG_ERR("present: display_write rc=%d", rc);
	}
}
