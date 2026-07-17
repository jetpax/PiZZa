/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- launcher menu.
 */

#ifndef PIZZA_RETRO_MENU_H
#define PIZZA_RETRO_MENU_H

#include <stddef.h>

/* Sentinel out_path from retro_menu_run: the user picked "Exit to
 * boot menu" (Pi boards only) -- caller hands off to bootsel.
 */
#define RETRO_MENU_EXIT_PATH "!bootmenu"

/* Render the core picker, drive it from the shim event queue (pad /
 * `retro key` shell), and block until the user launches a core.
 * Fills out_path with the chosen .llext path. Returns 0 on a launch,
 * negative if no cores are available.
 */
int retro_menu_run(char *out_path, size_t out_sz);

/* Browse RETRO_CONTENT_DIR filtered by the chosen core's valid_extensions
 * (pipe list, e.g. "gb|gbc"). title heads the screen (the core name).
 * Returns 0 with out_path filled on a pick, -1 if the user backs out with
 * Home. Blocks on an empty dir (rescanning) until a pick or a back.
 */
int retro_menu_browse_content(const char *title, const char *exts,
			      char *out_path, size_t out_sz);

#endif /* PIZZA_RETRO_MENU_H */
