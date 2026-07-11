/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban -- `sok` shell command set (USB CDC ACM).
 *
 * Interactive key injection: the input path on hardware until a HOGP
 * keyboard lands (the shim event seam already accepts any producer).
 * `sok key <name> [hold_ms]` submits KEYDOWN, holds on a one-shot
 * timer, then submits KEYUP -- the game latches held keys once per
 * frame, so the default hold spans several frames and a started move
 * completes on its own.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>

#include "sdl2shim.h"

#define SOK_DEFAULT_HOLD_MS 150

static struct k_timer keyup_timer;
static SDL_Scancode held_sc;

static const struct {
	const char *name;
	SDL_Scancode sc;
} key_map[] = {
	{ "up", SDL_SCANCODE_UP },
	{ "down", SDL_SCANCODE_DOWN },
	{ "left", SDL_SCANCODE_LEFT },
	{ "right", SDL_SCANCODE_RIGHT },
	{ "o", SDL_SCANCODE_O },
	{ "m", SDL_SCANCODE_M },
	{ "p", SDL_SCANCODE_P },
	{ "r", SDL_SCANCODE_R },
	{ "u", SDL_SCANCODE_U },
	{ "space", SDL_SCANCODE_SPACE },
	{ "esc", SDL_SCANCODE_ESCAPE },
};

static void submit_key(SDL_Scancode sc, Uint32 type)
{
	SDL_Event ev = { 0 };

	ev.type = type;
	ev.key.state = (type == SDL_KEYDOWN) ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.scancode = sc;
	s2s_event_submit(&ev);
}

static void keyup_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	submit_key(held_sc, SDL_KEYUP);
}

static int cmd_sok_key(const struct shell *sh, size_t argc, char **argv)
{
	int hold_ms = SOK_DEFAULT_HOLD_MS;

	if (argc >= 3) {
		hold_ms = atoi(argv[2]);
		if (hold_ms < 10 || hold_ms > 5000) {
			shell_error(sh, "hold_ms out of range (10..5000)");
			return -EINVAL;
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(key_map); i++) {
		if (strcmp(argv[1], key_map[i].name) == 0) {
			if (k_timer_remaining_get(&keyup_timer) > 0) {
				/* release the previous key first */
				k_timer_stop(&keyup_timer);
				submit_key(held_sc, SDL_KEYUP);
			}
			held_sc = key_map[i].sc;
			submit_key(held_sc, SDL_KEYDOWN);
			k_timer_start(&keyup_timer, K_MSEC(hold_ms), K_NO_WAIT);
			return 0;
		}
	}

	shell_error(sh, "unknown key '%s'", argv[1]);
	shell_print(sh, "keys: up down left right o m p r u space esc");
	return -EINVAL;
}

static int sok_shell_init(void)
{
	k_timer_init(&keyup_timer, keyup_expiry, NULL);
	return 0;
}

SYS_INIT(sok_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sok,
	SHELL_CMD_ARG(key, NULL,
		      "inject a key press: sok key <name> [hold_ms]",
		      cmd_sok_key, 2, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(sok, &sub_sok, "sokoban controls", NULL);
