/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sokoban core descriptor for the sdl2shim-over-libretro glue.
 *
 * Retropad -> sokoban keys, mirroring the app's btinput policy
 * (sokoban_btinput.c): dpad moves, A/START continue, B undo, X starts
 * Original, Y starts Microban, L restarts, R pauses. Nothing maps to
 * ESC -- quit stays a frontend affair.
 */

#include <SDL2/SDL.h>
#include <libretro.h>

#include "sdl2shim_libretro.h"

static const struct s2s_libretro_keybind sok_keymap[] = {
	{ RETRO_DEVICE_ID_JOYPAD_UP,     SDL_SCANCODE_UP },
	{ RETRO_DEVICE_ID_JOYPAD_DOWN,   SDL_SCANCODE_DOWN },
	{ RETRO_DEVICE_ID_JOYPAD_LEFT,   SDL_SCANCODE_LEFT },
	{ RETRO_DEVICE_ID_JOYPAD_RIGHT,  SDL_SCANCODE_RIGHT },
	{ RETRO_DEVICE_ID_JOYPAD_A,      SDL_SCANCODE_SPACE },
	{ RETRO_DEVICE_ID_JOYPAD_START,  SDL_SCANCODE_SPACE },
	{ RETRO_DEVICE_ID_JOYPAD_B,      SDL_SCANCODE_U },
	{ RETRO_DEVICE_ID_JOYPAD_X,      SDL_SCANCODE_O },
	{ RETRO_DEVICE_ID_JOYPAD_Y,      SDL_SCANCODE_M },
	{ RETRO_DEVICE_ID_JOYPAD_L,      SDL_SCANCODE_R },
	{ RETRO_DEVICE_ID_JOYPAD_R,      SDL_SCANCODE_P },
};

const struct s2s_libretro_desc s2s_libretro_desc = {
	.name = "sokoban",
	.version = "pizza-m3",
	.width = 640,
	.height = 480,
	.fps = 60.0,
	.sample_rate = 0.0,
	.keymap = sok_keymap,
	.keymap_len = sizeof(sok_keymap) / sizeof(sok_keymap[0]),
};
