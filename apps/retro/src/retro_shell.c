/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- `retro` shell command set (USB CDC ACM).
 *
 * Interactive retropad injection over the shim event seam.
 * `retro key <name> [hold_ms]` submits KEYDOWN, holds on a one-shot
 * timer, then submits KEYUP -- the core latches held keys once per
 * retro_run, so the default hold spans several frames.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>

#include "sdl2shim.h"

#define RETRO_DEFAULT_HOLD_MS 150

static struct k_timer keyup_timer;
static SDL_Scancode held_sc;

/* Names follow the retropad, not the underlying scancodes. */
static const struct {
	const char *name;
	SDL_Scancode sc;
} key_map[] = {
	{ "up", SDL_SCANCODE_UP },
	{ "down", SDL_SCANCODE_DOWN },
	{ "left", SDL_SCANCODE_LEFT },
	{ "right", SDL_SCANCODE_RIGHT },
	{ "start", SDL_SCANCODE_RETURN },
	{ "select", SDL_SCANCODE_TAB },
	{ "a", SDL_SCANCODE_X },
	{ "b", SDL_SCANCODE_Z },
	{ "x", SDL_SCANCODE_S },
	{ "y", SDL_SCANCODE_A },
	{ "l", SDL_SCANCODE_Q },
	{ "r", SDL_SCANCODE_W },
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

static int cmd_retro_key(const struct shell *sh, size_t argc, char **argv)
{
	int hold_ms = RETRO_DEFAULT_HOLD_MS;

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
	shell_print(sh, "keys: up down left right start select a b x y l r");
	return -EINVAL;
}

static int retro_shell_init(void)
{
	k_timer_init(&keyup_timer, keyup_expiry, NULL);
	return 0;
}

SYS_INIT(retro_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#ifdef CONFIG_RETRO_MENU
#include "retro_frontend.h"

static int cmd_retro_menu(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	retro_request_menu();
	shell_print(sh, "returning to launcher");
	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(sub_retro,
	SHELL_CMD_ARG(key, NULL,
		      "inject a retropad press: retro key <name> [hold_ms]",
		      cmd_retro_key, 2, 1),
#ifdef CONFIG_RETRO_MENU
	SHELL_CMD(menu, NULL, "tear down the running core, return to launcher",
		  cmd_retro_menu),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(retro, &sub_retro, "libretro frontend controls", NULL);
