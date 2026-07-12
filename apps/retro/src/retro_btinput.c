/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- Bluetooth HID input (CONFIG_BTINPUT).
 *
 * The whole BT stack lives in apps/lib/btinput (manager = bring-up +
 * connection policy on its own thread; SDL seam = events into the
 * sdl2shim queue the frontend drains). This TU is only consumer
 * policy: remap the 8BitDo Micro's stock K-mode letters onto the
 * scancodes the frontend's joypad table reads, so the pad IS the
 * retropad. Any real BT keyboard passes through unmapped -- HID
 * usages are SDL scancodes 1:1, so its arrows/RETURN/TAB just work.
 */

#include <zephyr/kernel.h>

#include <SDL2/SDL.h>

#include "btinput.h"

/* 8BitDo Micro, K mode, stock map (usage set confirmed on the DOSBox
 * and sokoban ports). Frontend joypad table: arrows = d-pad,
 * RETURN = START, TAB = SELECT, Z = B, X = A, A = Y, S = X,
 * Q/W = L/R. For 2048: A starts/pauses, B continues after a win.
 * Home fires during the pad's power-off long-press and the four
 * provisional usages are uncertain, so they're pinned to P -- a
 * scancode the frontend joypad table does not read at all (inert).
 */
static const struct btinput_seam_key pad_map[] = {
	{ 0x06, SDL_SCANCODE_UP },     /* d-pad Up    (C)              */
	{ 0x07, SDL_SCANCODE_DOWN },   /* d-pad Down  (D)              */
	{ 0x08, SDL_SCANCODE_LEFT },   /* d-pad Left  (E)              */
	{ 0x09, SDL_SCANCODE_RIGHT },  /* d-pad Right (F)              */
	{ 0x0a, SDL_SCANCODE_RETURN }, /* A (G) -> START               */
	{ 0x0d, SDL_SCANCODE_TAB },    /* B (J) -> SELECT              */
	{ 0x0b, SDL_SCANCODE_Z },      /* X (H) -> retropad B          */
	{ 0x0c, SDL_SCANCODE_S },      /* Y (I) -> retropad X          */
	{ 0x12, SDL_SCANCODE_Q },      /* L (O) -> retropad L          */
	{ 0x11, SDL_SCANCODE_W },      /* R (N) -> retropad R          */
	/* Home -> return to the launcher. A single button (no two-key
	 * rollover, unlike the L+R chord) mapped to ESCAPE, which is not
	 * in the frontend joypad table so cores never see it. Home also
	 * fires on the pad's power-off long-press: harmless now (it just
	 * pops the launcher), unlike the old game-quit behavior.
	 */
	{ 0x16, SDL_SCANCODE_ESCAPE }, /* Home -> launcher             */
	{ 0x0e, SDL_SCANCODE_P },      /* provisional -> inert         */
	{ 0x0f, SDL_SCANCODE_P },      /* provisional -> inert         */
	{ 0x10, SDL_SCANCODE_P },      /* provisional -> inert         */
	{ 0x15, SDL_SCANCODE_P },      /* provisional -> inert         */
};

void retro_btinput_start(void)
{
	btinput_seam_sdl_set_keymap(pad_map, ARRAY_SIZE(pad_map));
	btinput_seam_sdl_set_bounds(376, 464);
	btinput_seam_sdl_attach();
	btinput_manager_start();
}
