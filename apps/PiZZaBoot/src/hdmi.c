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

/* Classic GRUB 0.5.x look: VGA blue field, white text, light bar.
 * Deliberately distinct from the RetroPiZZa launcher's dark theme so
 * the two full-screen lists are never mistaken for each other.
 */
#define COL_BG       0xFF0000A8u
#define COL_TEXT     0xFFFFFFFFu
#define COL_MISSING  0xFF9098C0u
#define COL_SEL_BG   0xFFE8E8E8u
#define COL_SEL_TEXT 0xFF0000A8u
#define COL_HELP     0xFFB8B8B8u

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

void hdmi_render(const struct bootsel_entry *ents, int n, int sel,
		 const char *status)
{
	if (!hdmi_ok) {
		return;
	}

	const int item_scale = 2;
	const int row_h = 8 * item_scale + 8;
	const int box_x = 12;
	const int box_y = 44;
	const int box_w = FB_W - 2 * box_x;
	const int box_rows = 12;
	const int box_h = box_rows * row_h + 12;
	const int list_x = box_x + 16;
	const int list_top = box_y + 10;

	for (int i = 0; i < FB_W * FB_H; i++) {
		fb[i] = COL_BG;
	}

	draw_text(box_x, 14, "PiZZaBoot  version " PIZZABOOT_VERSION, 2,
		  COL_TEXT);

	fill_rect(box_x, box_y, box_w, 2, COL_TEXT);
	fill_rect(box_x, box_y + box_h - 2, box_w, 2, COL_TEXT);
	fill_rect(box_x, box_y, 2, box_h, COL_TEXT);
	fill_rect(box_x + box_w - 2, box_y, 2, box_h, COL_TEXT);

	if (n == 0) {
		draw_text(list_x, list_top, "no boot entries", item_scale,
			  COL_TEXT);
	}

	for (int i = 0; i < n && i < PB_MAX_ENTRIES; i++) {
		int y = list_top + i * row_h;
		uint32_t col = ents[i].present ? COL_TEXT : COL_MISSING;

		if (i == sel) {
			fill_rect(box_x + 2, y - 3, box_w - 4, row_h,
				  COL_SEL_BG);
			col = ents[i].present ? COL_SEL_TEXT : COL_MISSING;
		}
		draw_text(list_x, y, ents[i].name, item_scale, col);
		if (!ents[i].present) {
			draw_text(box_x + box_w - 100, y + 4, "(missing)", 1,
				  col == COL_SEL_TEXT ? COL_SEL_TEXT :
				  COL_MISSING);
		}
	}

	int hy = box_y + box_h + 16;

	draw_text(box_x + 8, hy,
		  "Use the up and down arrows (or short button presses) to",
		  1, COL_HELP);
	draw_text(box_x + 8, hy + 14,
		  "select which entry is highlighted.", 1, COL_HELP);
	draw_text(box_x + 8, hy + 34,
		  "Press enter or a digit, or hold the button, to boot the",
		  1, COL_HELP);
	draw_text(box_x + 8, hy + 48,
		  "selected image.", 1, COL_HELP);

	if (status != NULL && status[0] != '\0') {
		draw_text(box_x + 8, FB_H - 30, status, 1, COL_TEXT);
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
