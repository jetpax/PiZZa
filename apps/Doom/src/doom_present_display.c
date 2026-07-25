/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- display present backend (CONFIG_DOOM_PRESENT_DISPLAY).
 *
 * Pushes Doom's 320x200 XRGB8888 frame at the Zephyr display API and
 * lets the display driver own the scaling, so this backend is
 * board-agnostic. On rpi_zero_2w that driver is bcm2835_fb with a
 * 320x200 virtual framebuffer (320 * 4 = 1280 B/row, 64-byte aligned so
 * its contiguous-DMA fast path engages) and the VideoCore HVS scales to
 * the monitor's native mode; on fxh618_d4 it is the sunxi DE33 pipeline
 * with a 320x200 render surface scaled x5 into a fixed 1080p60 mode.
 * Either way the contract is the same: capabilities report the render
 * size, and a full frame goes out per present.
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
	LOG_INF("present: display %ux%u (render surface) fmt=0x%x",
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

	uint32_t t0 = k_cycle_get_32();
	int rc = display_write(display_dev, 0, 0, &desc, backbuf);
	uint32_t dt = k_cycle_get_32() - t0;

	if (rc != 0) {
		LOG_ERR("present: display_write rc=%d", rc);
	}

	/* Frame rate over the last 200 presents, and how much of the frame
	 * went into the display driver. The split matters: a driver that
	 * scales in software (fxh618_d4) charges a fixed cost per frame
	 * here, one that scales in hardware (rpi_zero_2w HVS) charges
	 * almost nothing, and the difference decides whether a frame-rate
	 * problem is the game or the scanout path.
	 */
	static uint32_t frames;
	static uint64_t write_cycles;
	static int64_t window_start;

	write_cycles += dt;
	if (++frames == 200U) {
		int64_t now = k_uptime_get();
		int64_t ms = now - window_start;
		uint32_t write_us = (uint32_t)(write_cycles /
			(sys_clock_hw_cycles_per_sec() / 1000000U) / frames);

		if (window_start != 0 && ms > 0) {
			LOG_INF("present: %u frames in %lld ms (%lld.%02lld fps), "
				"display_write %u us of %lld us per frame",
				frames, ms, 200000 / ms,
				(200000 * 100 / ms) % 100, write_us,
				ms * 1000 / frames);
		}
		window_start = now;
		frames = 0U;
		write_cycles = 0U;
	}
}
