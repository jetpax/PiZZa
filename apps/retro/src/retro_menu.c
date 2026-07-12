/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- launcher menu (CONFIG_RETRO_MENU).
 *
 * Frontend UI, not a core: scans CONFIG_RETRO_MENU_DIR for *.llext,
 * renders a full-screen picker into an ARGB buffer through the present
 * seam (same path a core's frames take), and navigates from the shim
 * event queue -- so the pad (btinput) and the `retro key` shell drive
 * it with no menu-specific input plumbing. Text is an embedded 8x8
 * bitmap font; no dependency on the shim's surface/atlas machinery.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include <stdlib.h>

#include "sdl2shim.h"
#include "retro_menu.h"
#include "retro_storage.h"
#include "retro_font8x8.h"

LOG_MODULE_REGISTER(retro_menu, CONFIG_RETRO_LOG_LEVEL);

#define MENU_W       CONFIG_RETRO_MENU_WIDTH
#define MENU_H       CONFIG_RETRO_MENU_HEIGHT
#define MENU_DIR     CONFIG_RETRO_MENU_DIR

#define MAX_ENTRIES  32
#define NAME_MAX     40
#define PATH_MAX     128

#define COL_BG       0xFF101822u
#define COL_TITLE    0xFFF0C020u
#define COL_TEXT     0xFFB8C0C8u
#define COL_SEL_BG   0xFF2C4058u
#define COL_SEL_TEXT 0xFFFFFFFFu
#define COL_HINT     0xFF60707Cu

struct entry {
	char name[NAME_MAX];   /* display: filename minus .llext */
	char path[PATH_MAX];   /* full path to load */
};

static struct entry entries[MAX_ENTRIES];
static int entry_count;
static uint32_t *fb;

/* --- rendering ------------------------------------------------------------- */

static void fill_rect(int x0, int y0, int w, int h, uint32_t argb)
{
	for (int y = y0; y < y0 + h && y < MENU_H; y++) {
		if (y < 0) {
			continue;
		}
		uint32_t *row = fb + (size_t)y * MENU_W;

		for (int x = x0; x < x0 + w && x < MENU_W; x++) {
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
		x += 8 * scale + scale; /* one-pixel (scaled) gap */
	}
}

static void render(int sel)
{
	const int title_scale = 4;
	const int item_scale = 3;
	const int item_h = 8 * item_scale + 10;
	const int list_top = 96;
	const int list_x = 48;

	for (int i = 0; i < MENU_W * MENU_H; i++) {
		fb[i] = COL_BG;
	}

	draw_text(list_x, 28, "PiZZa libretro", title_scale, COL_TITLE);

	if (entry_count == 0) {
		draw_text(list_x, list_top, "no cores in", item_scale, COL_TEXT);
		draw_text(list_x, list_top + item_h, MENU_DIR, 2, COL_HINT);
	}

	for (int i = 0; i < entry_count; i++) {
		int y = list_top + i * item_h;

		if (i == sel) {
			fill_rect(list_x - 16, y - 5, MENU_W - 2 * (list_x - 16),
				  item_h, COL_SEL_BG);
			draw_text(list_x, y, ">", item_scale, COL_SEL_TEXT);
		}
		draw_text(list_x + 8 * item_scale + 8, y, entries[i].name,
			  item_scale, i == sel ? COL_SEL_TEXT : COL_TEXT);
	}

	draw_text(list_x, MENU_H - 40,
		  "d-pad move   A launch   Home back", 2, COL_HINT);
}

/* --- directory scan -------------------------------------------------------- */

static int scan(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	int n = 0;

	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, MENU_DIR) != 0) {
		LOG_WRN("cannot open %s", MENU_DIR);
		return 0;
	}

	while (n < MAX_ENTRIES && fs_readdir(&dir, &ent) == 0 &&
	       ent.name[0] != '\0') {
		if (ent.type != FS_DIR_ENTRY_FILE) {
			continue;
		}
		/* Skip dotfiles -- notably macOS AppleDouble sidecars
		 * ("._2048.llext") that Finder writes next to each file on
		 * the FAT card; loading one as an llext would fail.
		 */
		if (ent.name[0] == '.') {
			continue;
		}
		size_t len = strlen(ent.name);
		const char *suf = ".llext";
		size_t slen = strlen(suf);

		if (len <= slen || strcmp(ent.name + len - slen, suf) != 0) {
			continue;
		}

		snprintf(entries[n].path, sizeof(entries[n].path), "%s/%s",
			 MENU_DIR, ent.name);
		size_t nlen = MIN(len - slen, sizeof(entries[n].name) - 1);

		memcpy(entries[n].name, ent.name, nlen);
		entries[n].name[nlen] = '\0';
		n++;
	}
	fs_closedir(&dir);
	return n;
}

/* --- input ----------------------------------------------------------------- */

/* Returns: 0 move up, 1 move down, 2 launch, -1 nothing. */
static int poll_action(void)
{
	SDL_Event e;
	int act = -1;

	while (SDL_PollEvent(&e)) {
		if (e.type != SDL_KEYDOWN) {
			continue;
		}
		switch (e.key.keysym.scancode) {
		case SDL_SCANCODE_UP:
			act = 0;
			break;
		case SDL_SCANCODE_DOWN:
			act = 1;
			break;
		case SDL_SCANCODE_X:      /* retropad A */
		case SDL_SCANCODE_RETURN: /* retropad START */
			act = 2;
			break;
		default:
			break;
		}
	}
	return act;
}

/* --- entry ----------------------------------------------------------------- */

int retro_menu_run(char *out_path, size_t out_sz)
{
	int sel = 0;

	if (fb == NULL) {
		fb = malloc((size_t)MENU_W * MENU_H * 4);
		if (fb == NULL) {
			LOG_ERR("menu framebuffer alloc failed");
			return -ENOMEM;
		}
	}

	retro_storage_mount();
	s2s_present_init(MENU_W, MENU_H);

	entry_count = scan();
	LOG_INF("%d core(s) in %s", entry_count, MENU_DIR);

	/* Drop any keys held from the L+R return chord. */
	SDL_Event drain;

	while (SDL_PollEvent(&drain)) {
	}

	for (;;) {
		int act = poll_action();

		if (entry_count == 0) {
			/* Nothing to launch: keep showing the notice and
			 * rescan in case storage settles.
			 */
			render(0);
			s2s_present_frame(fb, MENU_W * 4);
			k_msleep(500);
			entry_count = scan();
			continue;
		}

		if (act == 0) {
			sel = (sel + entry_count - 1) % entry_count;
		} else if (act == 1) {
			sel = (sel + 1) % entry_count;
		} else if (act == 2) {
			strncpy(out_path, entries[sel].path, out_sz - 1);
			out_path[out_sz - 1] = '\0';
			LOG_INF("launching %s", entries[sel].name);
			return 0;
		}

		render(sel);
		s2s_present_frame(fb, MENU_W * 4);
		k_msleep(16);
	}
}
