/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sdl2-doom core descriptor for the sdl2shim-over-libretro glue.
 *
 * Content core: the launcher browses /SD:/roms for "*.wad", and the glue
 * runs the game as "core -iwad <picked path>" (need_fullpath -- Doom
 * opens the file itself, doom_core_fs.c). Geometry is Doom's native
 * 320x200; the HVS upscales on hardware. Retropad -> Doom's default
 * keys (m_controls.c): arrows move/turn, A fire (LCTRL), B use (SPACE),
 * START/SELECT drive the menu, shoulders strafe.
 */

#include <SDL2/SDL.h>
#include <libretro.h>

#include "sdl2shim_libretro.h"

static const struct s2s_libretro_keybind doom_keymap[] = {
	{ RETRO_DEVICE_ID_JOYPAD_UP,     SDL_SCANCODE_UP },
	{ RETRO_DEVICE_ID_JOYPAD_DOWN,   SDL_SCANCODE_DOWN },
	{ RETRO_DEVICE_ID_JOYPAD_LEFT,   SDL_SCANCODE_LEFT },
	{ RETRO_DEVICE_ID_JOYPAD_RIGHT,  SDL_SCANCODE_RIGHT },
	{ RETRO_DEVICE_ID_JOYPAD_A,      SDL_SCANCODE_LCTRL },   /* fire */
	{ RETRO_DEVICE_ID_JOYPAD_B,      SDL_SCANCODE_SPACE },   /* use/open */
	{ RETRO_DEVICE_ID_JOYPAD_X,      SDL_SCANCODE_RSHIFT },  /* run */
	{ RETRO_DEVICE_ID_JOYPAD_Y,      SDL_SCANCODE_TAB },     /* automap */
	{ RETRO_DEVICE_ID_JOYPAD_START,  SDL_SCANCODE_RETURN },  /* menu select */
	{ RETRO_DEVICE_ID_JOYPAD_SELECT, SDL_SCANCODE_ESCAPE },  /* menu */
	{ RETRO_DEVICE_ID_JOYPAD_L,      SDL_SCANCODE_COMMA },   /* strafe left */
	{ RETRO_DEVICE_ID_JOYPAD_R,      SDL_SCANCODE_PERIOD },  /* strafe right */
};

const struct s2s_libretro_desc s2s_libretro_desc = {
	.name = "doom",
	.version = "pizza-m5",
	.width = 320,
	.height = 200,
	.fps = 35.0,
	.sample_rate = 0.0,
	.keymap = doom_keymap,
	.keymap_len = sizeof(doom_keymap) / sizeof(doom_keymap[0]),

	.needs_content = true,
	.need_fullpath = true,
	.valid_extensions = "wad",
	.content_arg = "-iwad",
};
