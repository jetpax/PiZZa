/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZaBoot -- HDMI list renderer.
 *
 * Draws the boot menu into a static 640x480 ARGB buffer and pushes it
 * through the Zephyr display API (bcm2835_fb; the board overlay sets
 * render-width/render-height 640x480 so the VideoCore HVS upscales to
 * the monitor's native mode). Text is the same public-domain 8x8 font
 * the RetroPiZZa launcher uses; drawing code cribbed from
 * apps/retro/src/retro_menu.c.
 *
 * Headless (no monitor / display init failure) is a supported mode:
 * hdmi_init() failing just disables rendering, the serial menu is
 * unaffected.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "pizzaboot.h"
#include "retro_font8x8.h"

#define FB_W 640
#define FB_H 480

#define COL_BG       0xFF101822u
#define COL_TITLE    0xFFF0C020u
#define COL_TEXT     0xFFB8C0C8u
#define COL_MISSING  0xFF5A6068u
#define COL_SEL_BG   0xFF2C4058u
#define COL_SEL_TEXT 0xFFFFFFFFu
#define COL_HINT     0xFF60707Cu

static const struct device *display_dev;
static bool hdmi_ok;
static uint32_t fb[FB_W * FB_H];

static void fill_rect(int x0, int y0, int w, int h, uint32_t argb)
{
	for (int y = y0; y < y0 + h && y < FB_H; y++) {
		if (y < 0) {
			continue;
		}
		uint32_t *row = fb + (size_t)y * FB_W;

		for (int x = x0; x < x0 + w && x < FB_W; x++) {
			if (x >= 0) {
				row[x] = argb;
			}
		}
	}
}

static void draw_char(int x, int y, char c, int scale, uint32_t argb)
{
	if ((uint8_t)c < RETRO_FONT_FIRST || (uint8_t)c > RETRO_FONT_LAST) {
		c = '?';
	}
	const uint8_t *g = retro_font8x8[(uint8_t)c - RETRO_FONT_FIRST];

	for (int row = 0; row < 8; row++) {
		for (int col = 0; col < 8; col++) {
			if (g[row] & (1u << col)) {
				fill_rect(x + col * scale, y + row * scale,
					  scale, scale, argb);
			}
		}
	}
}

static void draw_text(int x, int y, const char *s, int scale, uint32_t argb)
{
	for (; *s != '\0'; s++) {
		draw_char(x, y, *s, scale, argb);
		x += 8 * scale + scale;
	}
}

int hdmi_init(void)
{
	if (!DT_HAS_CHOSEN(zephyr_display)) {
		return -ENODEV;
	}
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		printk("[pizzaboot] display not ready, HDMI menu off\n");
		return -ENODEV;
	}

	struct display_capabilities caps;

	display_get_capabilities(display_dev, &caps);
	if (caps.x_resolution != FB_W || caps.y_resolution != FB_H) {
		printk("[pizzaboot] display %ux%u != %ux%u, HDMI menu off\n",
		       caps.x_resolution, caps.y_resolution, FB_W, FB_H);
		return -EINVAL;
	}

	hdmi_ok = true;
	return 0;
}

void hdmi_render(const struct pb_entry *ents, int n, int sel,
		 const char *status)
{
	if (!hdmi_ok) {
		return;
	}

	const int item_scale = 3;
	const int item_h = 8 * item_scale + 10;
	const int list_top = 110;
	const int list_x = 48;

	for (int i = 0; i < FB_W * FB_H; i++) {
		fb[i] = COL_BG;
	}

	draw_text(list_x, 28, "PiZZaBoot", 4, COL_TITLE);

	if (n == 0) {
		draw_text(list_x, list_top, "no boot entries", item_scale,
			  COL_TEXT);
	}

	for (int i = 0; i < n && i < PB_MAX_ENTRIES; i++) {
		int y = list_top + i * item_h;
		uint32_t col = ents[i].present ? COL_TEXT : COL_MISSING;

		if (i == sel) {
			fill_rect(list_x - 16, y - 5,
				  FB_W - 2 * (list_x - 16), item_h,
				  COL_SEL_BG);
			col = ents[i].present ? COL_SEL_TEXT : COL_MISSING;
			draw_text(list_x, y, ">", item_scale, COL_SEL_TEXT);
		}
		draw_text(list_x + 8 * item_scale + 8, y, ents[i].name,
			  item_scale, col);
		if (!ents[i].present) {
			draw_text(FB_W - 190, y, "missing", 2, COL_MISSING);
		}
	}

	draw_text(list_x, FB_H - 60,
		  "button: short next / long boot", 1, COL_HINT);
	if (status != NULL && status[0] != '\0') {
		draw_text(list_x, FB_H - 36, status, 1, COL_TITLE);
	}

	struct display_buffer_descriptor desc = {
		.buf_size = (size_t)FB_W * FB_H * 4,
		.width = FB_W,
		.height = FB_H,
		.pitch = FB_W,
		.frame_incomplete = false,
	};

	(void)display_write(display_dev, 0, 0, &desc, fb);
}
