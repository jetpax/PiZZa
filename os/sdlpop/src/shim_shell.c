/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDLPoP port -- `pop` shell command set (USB CDC ACM).
 *
 * Injects key events into the game through the same shim_event.c
 * producer seam the scripted source and the future HOGP keyboard
 * client use (work-order §5b). This is the interactive control path
 * on hardware until the Bluetooth keyboard lands -- you drive Prince
 * of Persia over the serial line.
 *
 *   pop key <name>       tap: keydown + keyup (menus, "press any key")
 *   pop down <name>      press and HOLD (movement -- left/right/up/shift)
 *   pop up <name>        release a held key
 *   pop tap <name> [ms]  hold <name> for ms (default 250) then release
 *   pop keys             list key names
 *
 * <name>: left right up down  return|enter  esc  space  shift  ctrl
 *         a..z  and the digits 0..9.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp (picolibc keeps it here) */

#include "pop_shim.h"

struct keyname {
	const char *name;
	SDL_Scancode sc;
};

static const struct keyname keymap[] = {
	{ "left", SDL_SCANCODE_LEFT },   { "right", SDL_SCANCODE_RIGHT },
	{ "up", SDL_SCANCODE_UP },       { "down", SDL_SCANCODE_DOWN },
	{ "return", SDL_SCANCODE_RETURN }, { "enter", SDL_SCANCODE_RETURN },
	{ "esc", SDL_SCANCODE_ESCAPE },  { "escape", SDL_SCANCODE_ESCAPE },
	{ "space", SDL_SCANCODE_SPACE }, { "tab", SDL_SCANCODE_TAB },
	{ "backspace", SDL_SCANCODE_BACKSPACE },
	{ "shift", SDL_SCANCODE_LSHIFT }, { "lshift", SDL_SCANCODE_LSHIFT },
	{ "rshift", SDL_SCANCODE_RSHIFT },
	{ "ctrl", SDL_SCANCODE_LCTRL },  { "lctrl", SDL_SCANCODE_LCTRL },
	{ "alt", SDL_SCANCODE_LALT },
	{ "home", SDL_SCANCODE_HOME },   { "end", SDL_SCANCODE_END },
	{ "pageup", SDL_SCANCODE_PAGEUP }, { "pagedown", SDL_SCANCODE_PAGEDOWN },
};

static SDL_Scancode lookup(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(keymap); i++) {
		if (strcasecmp(name, keymap[i].name) == 0) {
			return keymap[i].sc;
		}
	}
	/* single letter a..z */
	if (name[0] && name[1] == '\0') {
		char c = name[0];

		if (c >= 'a' && c <= 'z') {
			return (SDL_Scancode)(SDL_SCANCODE_A + (c - 'a'));
		}
		if (c >= 'A' && c <= 'Z') {
			return (SDL_Scancode)(SDL_SCANCODE_A + (c - 'A'));
		}
		if (c >= '1' && c <= '9') {
			return (SDL_Scancode)(SDL_SCANCODE_1 + (c - '1'));
		}
		if (c == '0') {
			return SDL_SCANCODE_0;
		}
	}
	return SDL_SCANCODE_UNKNOWN;
}

static void submit(SDL_Scancode sc, bool down)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.scancode = sc;
	pop_event_submit(&ev);
}

static int arg_key(const struct shell *sh, char *arg, SDL_Scancode *out)
{
	SDL_Scancode sc = lookup(arg);

	if (sc == SDL_SCANCODE_UNKNOWN) {
		shell_error(sh, "unknown key '%s' (try `pop keys`)", arg);
		return -EINVAL;
	}
	*out = sc;
	return 0;
}

static int cmd_key(const struct shell *sh, size_t argc, char **argv)
{
	SDL_Scancode sc;

	if (argc < 2 || arg_key(sh, argv[1], &sc)) {
		return -EINVAL;
	}
	submit(sc, true);
	submit(sc, false);
	shell_print(sh, "tapped %s", argv[1]);
	return 0;
}

static int cmd_down(const struct shell *sh, size_t argc, char **argv)
{
	SDL_Scancode sc;

	if (argc < 2 || arg_key(sh, argv[1], &sc)) {
		return -EINVAL;
	}
	submit(sc, true);
	shell_print(sh, "holding %s (pop up %s to release)", argv[1], argv[1]);
	return 0;
}

static int cmd_up(const struct shell *sh, size_t argc, char **argv)
{
	SDL_Scancode sc;

	if (argc < 2 || arg_key(sh, argv[1], &sc)) {
		return -EINVAL;
	}
	submit(sc, false);
	shell_print(sh, "released %s", argv[1]);
	return 0;
}

static int cmd_tap(const struct shell *sh, size_t argc, char **argv)
{
	SDL_Scancode sc;

	if (argc < 2 || arg_key(sh, argv[1], &sc)) {
		return -EINVAL;
	}

	int ms = (argc >= 3) ? atoi(argv[2]) : 250;

	if (ms <= 0) {
		ms = 250;
	}
	submit(sc, true);
	k_msleep(ms);
	submit(sc, false);
	shell_print(sh, "held %s for %d ms", argv[1], ms);
	return 0;
}

static int cmd_keys(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "left right up down  return/enter  esc  space  tab  "
			"backspace  shift  ctrl  alt  home end pageup pagedown  "
			"a..z  0..9");
	shell_print(sh, "PoP: arrows move, shift = careful/grab, enter = "
			"start/continue, esc = menu");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pop,
	SHELL_CMD_ARG(key,  NULL, "<name>  -- tap (down+up), e.g. `pop key enter`", cmd_key, 2, 0),
	SHELL_CMD_ARG(down, NULL, "<name>  -- press and hold",  cmd_down, 2, 0),
	SHELL_CMD_ARG(up,   NULL, "<name>  -- release a held key", cmd_up, 2, 0),
	SHELL_CMD_ARG(tap,  NULL, "<name> [ms]  -- hold then release (default 250 ms)", cmd_tap, 2, 1),
	SHELL_CMD(keys,     NULL, "list key names", cmd_keys),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(pop, &sub_pop, "PiZZa SDLPoP input control (inject keys)", NULL);
