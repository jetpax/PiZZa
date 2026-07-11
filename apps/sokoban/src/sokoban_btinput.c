/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban -- Bluetooth HID input (CONFIG_BTINPUT).
 *
 * The whole BT stack lives in apps/lib/btinput (manager = bring-up +
 * connection policy on its own thread; SDL seam = events into the
 * sdl2shim queue App::Run already drains). This TU is only consumer
 * policy: remap the 8BitDo Micro's stock K-mode letters onto sokoban's
 * keys. Any real BT keyboard passes through unmapped -- HID usages are
 * SDL scancodes 1:1, so its actual O/M/R/U/arrow keys just work.
 */

#include <zephyr/kernel.h>

#include <SDL2/SDL.h>

#include "btinput.h"

/* 8BitDo Micro, K mode, stock map (same usage set the DOSBox port
 * confirmed). The four provisional usages (0x0e/0x0f/0x10/0x15 --
 * L2/R2/Plus/Minus/Star in some order) are pinned to keys sokoban
 * IGNORES on purpose: unmapped they'd pass through as their raw
 * letters k/l/m/r, and stray 'm' starts Microban / 'r' restarts the
 * level.
 */
static const struct btinput_seam_key pad_map[] = {
	{ 0x06, SDL_SCANCODE_UP },     /* d-pad Up    (C)             */
	{ 0x07, SDL_SCANCODE_DOWN },   /* d-pad Down  (D)             */
	{ 0x08, SDL_SCANCODE_LEFT },   /* d-pad Left  (E)             */
	{ 0x09, SDL_SCANCODE_RIGHT },  /* d-pad Right (F)             */
	{ 0x0a, SDL_SCANCODE_SPACE },  /* A (G) -> continue           */
	{ 0x0d, SDL_SCANCODE_U },      /* B (J) -> undo move          */
	{ 0x0b, SDL_SCANCODE_O },      /* X (H) -> start Original     */
	{ 0x0c, SDL_SCANCODE_M },      /* Y (I) -> start Microban     */
	{ 0x12, SDL_SCANCODE_R },      /* L (O) -> restart level      */
	{ 0x11, SDL_SCANCODE_P },      /* R (N) -> pause              */
	{ 0x16, SDL_SCANCODE_ESCAPE }, /* Home (also power-off press) */
	{ 0x0e, SDL_SCANCODE_RETURN }, /* provisional -> inert        */
	{ 0x0f, SDL_SCANCODE_RETURN }, /* provisional -> inert        */
	{ 0x10, SDL_SCANCODE_TAB },    /* provisional -> inert        */
	{ 0x15, SDL_SCANCODE_TAB },    /* provisional -> inert        */
};

void sokoban_btinput_start(void)
{
	btinput_seam_sdl_set_keymap(pad_map, ARRAY_SIZE(pad_map));
	btinput_seam_sdl_set_bounds(640, 480);
	btinput_seam_sdl_attach();
	btinput_manager_start();
}
