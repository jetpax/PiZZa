/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PiZZa Faux86 -- display present backend (CONFIG_FAUX86_PRESENT_DISPLAY).
 *
 * Faux86 changes video mode at runtime (XT text 640x400, CGA 320x200,
 * EGA 640x350, ...), but the Zephyr display API has a fixed resolution
 * set at probe. So the VC framebuffer is pinned at a fixed 640x400
 * virtual mode (render-width/height in the overlay) and each incoming
 * frame is nearest-neighbor scaled into it; the VideoCore HVS then
 * scales 640x400 to the monitor's native mode -- the same proven trick
 * as the Jet / wipeout / SDLPoP / Doom / DOSBox-X ports.
 *
 * 640x400 is chosen so the XT text mode (80x25, 8x16 font) lands 1:1.
 * The host blit hands over 0x00RRGGBB; scanout wants 0xFFRRGGBB, so
 * alpha is forced opaque.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

#include "faux86_present.h"

LOG_MODULE_REGISTER(faux86_present, LOG_LEVEL_INF);

#define FB_W 640
#define FB_H 400

static const struct device *display_dev;
static uint32_t backbuf[FB_W * FB_H];
static bool ready;

static int src_w, src_h;
static uint16_t xmap[FB_W]; /* src column for each dst column */

int s2s_present_init(int width, int height)
{
	src_w = width;
	src_h = height;

	for (int x = 0; x < FB_W; x++) {
		int sx = (int)((int64_t)x * src_w / FB_W);

		xmap[x] = (uint16_t)(sx < src_w ? sx : src_w - 1);
	}

	if (ready) {
		LOG_INF("present: mode -> %dx%d (VFB %dx%d)", src_w, src_h, FB_W, FB_H);
		return 0;
	}

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("present: display device not ready -- HDMI not connected?");
		return -ENODEV;
	}

	struct display_capabilities caps;

	display_get_capabilities(display_dev, &caps);
	LOG_INF("present: VFB %ux%u (HVS virt) fmt=0x%x; first src %dx%d",
		caps.x_resolution, caps.y_resolution,
		(unsigned int)caps.current_pixel_format, src_w, src_h);

	if (caps.x_resolution != FB_W || caps.y_resolution != FB_H) {
		LOG_WRN("present: display reports %ux%u, expected %ux%u "
			"(check render-width/render-height in overlay)",
			caps.x_resolution, caps.y_resolution, FB_W, FB_H);
	}

	ready = true;
	return 0;
}

void s2s_present_frame(const void *xrgb_pixels, int pitch)
{
	if (!ready || src_w <= 0 || src_h <= 0) {
		return;
	}

	const uint8_t *src_base = xrgb_pixels;
	uint32_t *dst = backbuf;

	for (int y = 0; y < FB_H; y++) {
		int sy = (int)((int64_t)y * src_h / FB_H);

		if (sy >= src_h) {
			sy = src_h - 1;
		}
		const uint32_t *srow = (const uint32_t *)(src_base + (size_t)sy * pitch);

		if (src_w == FB_W) {
			/* text-mode fast path: 1:1 columns, just force alpha */
			for (int x = 0; x < FB_W; x++) {
				dst[x] = 0xFF000000u | (srow[x] & 0x00FFFFFFu);
			}
		} else {
			for (int x = 0; x < FB_W; x++) {
				dst[x] = 0xFF000000u | (srow[xmap[x]] & 0x00FFFFFFu);
			}
		}
		dst += FB_W;
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(backbuf),
		.width = FB_W,
		.height = FB_H,
		.pitch = FB_W,
		.frame_incomplete = false,
	};

	int rc = display_write(display_dev, 0, 0, &desc, backbuf);

	if (rc != 0) {
		LOG_ERR("present: display_write rc=%d", rc);
	}
}
