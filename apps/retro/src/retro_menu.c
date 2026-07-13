/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- launcher menu (CONFIG_RETRO_MENU).
 *
 * Frontend UI, not a core: a generic list-picker rendered full-screen
 * into an ARGB buffer through the present seam (same path a core's frames
 * take) and driven from the shim event queue -- so the pad (btinput) and
 * the `retro key` shell drive it with no menu-specific input plumbing.
 * Two users share it: the core picker (scans RETRO_MENU_DIR for *.llext)
 * and the content browser (scans RETRO_CONTENT_DIR filtered by the chosen
 * core's valid_extensions). Text is an embedded 8x8 bitmap font; no
 * dependency on the shim's surface/atlas machinery.
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

#define MAX_ENTRIES  64
#define NAME_MAX     40
#define PATH_MAX     128

#define COL_BG       0xFF101822u
#define COL_TITLE    0xFFF0C020u
#define COL_TEXT     0xFFB8C0C8u
#define COL_SEL_BG   0xFF2C4058u
#define COL_SEL_TEXT 0xFFFFFFFFu
#define COL_HINT     0xFF60707Cu

/* poll_action() verbs. */
#define ACT_NONE   (-1)
#define ACT_UP     0
#define ACT_DOWN   1
#define ACT_LAUNCH 2
#define ACT_BACK   3

struct entry {
	char name[NAME_MAX];   /* display label */
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

static void render(const char *title, const char *empty_hint,
		   const char *footer, int sel)
{
	const int title_scale = 4;
	const int item_scale = 3;
	const int item_h = 8 * item_scale + 10;
	const int list_top = 96;
	const int list_x = 48;

	for (int i = 0; i < MENU_W * MENU_H; i++) {
		fb[i] = COL_BG;
	}

	draw_text(list_x, 28, title, title_scale, COL_TITLE);

	if (entry_count == 0) {
		draw_text(list_x, list_top, "nothing in", item_scale, COL_TEXT);
		draw_text(list_x, list_top + item_h, empty_hint, 2, COL_HINT);
	}

	/* Scroll so the selection stays on screen for long lists. */
	const int rows = (MENU_H - list_top - 48) / item_h;
	int first = 0;

	if (entry_count > rows) {
		first = sel - rows / 2;
		if (first < 0) {
			first = 0;
		}
		if (first > entry_count - rows) {
			first = entry_count - rows;
		}
	}

	for (int i = first; i < entry_count && i < first + rows; i++) {
		int y = list_top + (i - first) * item_h;

		if (i == sel) {
			fill_rect(list_x - 16, y - 5, MENU_W - 2 * (list_x - 16),
				  item_h, COL_SEL_BG);
			draw_text(list_x, y, ">", item_scale, COL_SEL_TEXT);
		}
		draw_text(list_x + 8 * item_scale + 8, y, entries[i].name,
			  item_scale, i == sel ? COL_SEL_TEXT : COL_TEXT);
	}

	draw_text(list_x, MENU_H - 40, footer, 2, COL_HINT);
}

/* --- directory scan -------------------------------------------------------- */

/* Case-insensitive ASCII compare of n chars (ROM extensions are ASCII). */
static bool ext_eq_ci(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		char ca = a[i], cb = b[i];

		if (ca >= 'A' && ca <= 'Z') {
			ca += 32;
		}
		if (cb >= 'A' && cb <= 'Z') {
			cb += 32;
		}
		if (ca != cb) {
			return false;
		}
	}
	return true;
}

/* ext (no dot) is one of the pipe-delimited tokens in list ("gb|gbc"). */
static bool ext_in_list(const char *ext, const char *list)
{
	size_t elen = strlen(ext);
	const char *p = list;

	while (*p != '\0') {
		const char *q = p;

		while (*q != '\0' && *q != '|') {
			q++;
		}
		if ((size_t)(q - p) == elen && ext_eq_ci(ext, p, elen)) {
			return true;
		}
		p = (*q == '|') ? q + 1 : q;
	}
	return false;
}

/* Scan dir for files whose extension is in exts, into entries[]. strip
 * drops the ".<ext>" from the display label (cores show "2048", content
 * shows "tetris.gb"). Returns the count.
 */
static int scan(const char *dir, const char *exts, bool strip)
{
	struct fs_dir_t d;
	struct fs_dirent ent;
	int n = 0;

	fs_dir_t_init(&d);
	if (fs_opendir(&d, dir) != 0) {
		LOG_WRN("cannot open %s", dir);
		return 0;
	}

	while (n < MAX_ENTRIES && fs_readdir(&d, &ent) == 0 &&
	       ent.name[0] != '\0') {
		if (ent.type != FS_DIR_ENTRY_FILE) {
			continue;
		}
		/* Skip dotfiles -- notably macOS AppleDouble sidecars
		 * ("._2048.llext") that Finder writes next to each file on
		 * the FAT card.
		 */
		if (ent.name[0] == '.') {
			continue;
		}

		const char *dot = strrchr(ent.name, '.');

		if (dot == NULL || !ext_in_list(dot + 1, exts)) {
			continue;
		}

		snprintf(entries[n].path, sizeof(entries[n].path), "%s/%s",
			 dir, ent.name);

		size_t nlen = strip ? (size_t)(dot - ent.name) : strlen(ent.name);

		nlen = MIN(nlen, sizeof(entries[n].name) - 1);
		memcpy(entries[n].name, ent.name, nlen);
		entries[n].name[nlen] = '\0';
		n++;
	}
	fs_closedir(&d);
	return n;
}

/* --- input ----------------------------------------------------------------- */

static int poll_action(void)
{
	SDL_Event e;
	int act = ACT_NONE;

	while (SDL_PollEvent(&e)) {
		if (e.type != SDL_KEYDOWN) {
			continue;
		}
		switch (e.key.keysym.scancode) {
		case SDL_SCANCODE_UP:
			act = ACT_UP;
			break;
		case SDL_SCANCODE_DOWN:
			act = ACT_DOWN;
			break;
		case SDL_SCANCODE_X:      /* retropad A */
		case SDL_SCANCODE_RETURN: /* retropad START */
			act = ACT_LAUNCH;
			break;
		case SDL_SCANCODE_ESCAPE: /* pad Home */
			act = ACT_BACK;
			break;
		default:
			break;
		}
	}
	return act;
}

/* --- shared picker --------------------------------------------------------- */

/* Present a scrolling list of dir's matching files; block until the user
 * launches one (0, out_path filled) or -- when allow_back -- backs out with
 * Home (-1). An empty list keeps rescanning (storage may still be settling)
 * while honouring Home.
 */
static int run_picker(const char *title, const char *dir, const char *exts,
		      bool strip, const char *footer, bool allow_back,
		      char *out_path, size_t out_sz)
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

	entry_count = scan(dir, exts, strip);
	LOG_INF("%d entr%s in %s", entry_count,
		entry_count == 1 ? "y" : "ies", dir);

	/* Drop any keys held from the return chord / previous screen. */
	SDL_Event drain;

	while (SDL_PollEvent(&drain)) {
	}

	for (;;) {
		int act = poll_action();

		if (allow_back && act == ACT_BACK) {
			return -1;
		}

		if (entry_count == 0) {
			render(title, dir, footer, 0);
			s2s_present_frame(fb, MENU_W * 4);
			k_msleep(200);
			entry_count = scan(dir, exts, strip);
			continue;
		}

		if (sel >= entry_count) {
			sel = entry_count - 1;
		}

		if (act == ACT_UP) {
			sel = (sel + entry_count - 1) % entry_count;
		} else if (act == ACT_DOWN) {
			sel = (sel + 1) % entry_count;
		} else if (act == ACT_LAUNCH) {
			strncpy(out_path, entries[sel].path, out_sz - 1);
			out_path[out_sz - 1] = '\0';
			LOG_INF("selected %s", entries[sel].name);
			return 0;
		}

		render(title, dir, footer, sel);
		s2s_present_frame(fb, MENU_W * 4);
		k_msleep(16);
	}
}

/* --- entry points ---------------------------------------------------------- */

int retro_menu_run(char *out_path, size_t out_sz)
{
	return run_picker("RetroPiZZa", MENU_DIR, "llext", true,
			  "d-pad move   A launch", false, out_path, out_sz);
}

int retro_menu_browse_content(const char *title, const char *exts,
			      char *out_path, size_t out_sz)
{
	return run_picker(title != NULL ? title : "Select content",
			  CONFIG_RETRO_CONTENT_DIR, exts, false,
			  "A load   Home back", true, out_path, out_sz);
}
