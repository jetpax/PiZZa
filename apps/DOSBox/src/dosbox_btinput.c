/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa DOSBox-X -- Bluetooth HID input (CONFIG_BTINPUT).
 *
 * The whole BT stack lives in apps/lib/btinput: the manager owns bring-up +
 * connection policy, the SDL seam feeds events into the sdl2shim queue that
 * GFX_Events already drains. This TU is only consumer policy: the 8BitDo
 * Micro's stock K-mode keymap is letters (C..S), so remap its buttons onto
 * the keys DOOM (and DOS games generally) expect. Any other BT keyboard
 * passes through unmapped -- HID usages are SDL scancodes 1:1.
 */

#include <zephyr/kernel.h>

#include <SDL2/SDL.h>

#include "btinput.h"

/* 8BitDo Micro, K mode, stock map (HANDOVER §4). Usages 0x0e/0x0f/0x10/0x15
 * surfaced in a free-mash and belong to L2/R2/Plus/Minus/Star in some order
 * (the one-at-a-time mini-pass is still owed); their mappings below are
 * provisional. Home (0x16) also fires during the pad's power-off long-press;
 * Escape is harmless there.
 */
static const struct btinput_seam_key pad_map[] = {
	{ 0x06, SDL_SCANCODE_UP },     /* d-pad Up    (C)          */
	{ 0x07, SDL_SCANCODE_DOWN },   /* d-pad Down  (D)          */
	{ 0x08, SDL_SCANCODE_LEFT },   /* d-pad Left  (E)          */
	{ 0x09, SDL_SCANCODE_RIGHT },  /* d-pad Right (F)          */
	{ 0x0a, SDL_SCANCODE_LCTRL },  /* A (G) -> fire            */
	{ 0x0d, SDL_SCANCODE_SPACE },  /* B (J) -> use/open        */
	{ 0x0b, SDL_SCANCODE_RETURN }, /* X (H) -> menu select     */
	{ 0x0c, SDL_SCANCODE_ESCAPE }, /* Y (I) -> menu            */
	{ 0x12, SDL_SCANCODE_COMMA },  /* L (O) -> strafe left     */
	{ 0x11, SDL_SCANCODE_PERIOD }, /* R (N) -> strafe right    */
	{ 0x16, SDL_SCANCODE_ESCAPE }, /* Home                     */
	{ 0x0e, SDL_SCANCODE_Y },      /* provisional: 'y' confirm */
	{ 0x0f, SDL_SCANCODE_N },      /* provisional: 'n' cancel  */
	{ 0x10, SDL_SCANCODE_RETURN }, /* provisional              */
	{ 0x15, SDL_SCANCODE_TAB },    /* provisional: automap     */
};

void dbx_btinput_start(void)
{
	btinput_seam_sdl_set_keymap(pad_map, ARRAY_SIZE(pad_map));
	btinput_seam_sdl_set_bounds(640, 400);
	btinput_seam_sdl_attach();
	btinput_manager_start();
}
