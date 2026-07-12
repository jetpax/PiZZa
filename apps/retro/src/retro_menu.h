/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- launcher menu.
 */

#ifndef PIZZA_RETRO_MENU_H
#define PIZZA_RETRO_MENU_H

#include <stddef.h>

/* Render the core picker, drive it from the shim event queue (pad /
 * `retro key` shell), and block until the user launches a core.
 * Fills out_path with the chosen .llext path. Returns 0 on a launch,
 * negative if no cores are available.
 */
int retro_menu_run(char *out_path, size_t out_sz);

#endif /* PIZZA_RETRO_MENU_H */
