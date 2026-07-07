/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- `minivmac` shell command set (USB CDC ACM / console).
 *
 * Injects keyboard + mouse events through the same shim_event.c producer
 * seam the scripted source and a future BT gamepad / HOGP client use. This is
 * the interactive control path on hardware -- you drive the Mac over the
 * serial line until a Bluetooth pointer lands.
 *
 *   minivmac key <name>        tap: keydown + keyup
 *   minivmac down <name>       press and HOLD (e.g. shift, cmd)
 *   minivmac up <name>         release a held key
 *   minivmac tap <name> [ms]   hold <name> for ms (default 250) then release
 *   minivmac mouse <x> <y>     move the cursor to absolute 512x342 coords
 *   minivmac moverel <dx> <dy> move the cursor by a delta
 *   minivmac click [x y]       click (down+up) at [x y] or the current spot
 *   minivmac keys              list key names
 *
 * <name>: left right up down  return|enter esc space tab backspace
 *         shift ctrl alt|option cmd|gui  a..z  0..9
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp (picolibc keeps it here) */

#include "sdl2shim.h"

#define MAC_W 512
#define MAC_H 342

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
	{ "alt", SDL_SCANCODE_LALT },    { "option", SDL_SCANCODE_LALT },
	{ "cmd", SDL_SCANCODE_LGUI },    { "gui", SDL_SCANCODE_LGUI },
	{ "command", SDL_SCANCODE_LGUI },
};

static SDL_Scancode lookup(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(keymap); i++) {
		if (strcasecmp(name, keymap[i].name) == 0) {
			return keymap[i].sc;
		}
	}
	/* single letter a..z or digit 0..9 */
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

static void submit_key(SDL_Scancode sc, bool down)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.scancode = sc;
	s2s_event_submit(&ev);
}

static int clampi(int v, int lo, int hi)
{
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void submit_motion(int x, int y)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = SDL_MOUSEMOTION;
	ev.motion.x = clampi(x, 0, MAC_W - 1);
	ev.motion.y = clampi(y, 0, MAC_H - 1);
	s2s_event_submit(&ev);
}

static void submit_button(int x, int y, bool down)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.button = SDL_BUTTON_LEFT;
	ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.button.x = clampi(x, 0, MAC_W - 1);
	ev.button.y = clampi(y, 0, MAC_H - 1);
	s2s_event_submit(&ev);
}

static int arg_key(const struct shell *sh, char *arg, SDL_Scancode *out)
{
	SDL_Scancode sc = lookup(arg);

	if (sc == SDL_SCANCODE_UNKNOWN) {
		shell_error(sh, "unknown key '%s' (try `minivmac keys`)", arg);
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
	submit_key(sc, true);
	submit_key(sc, false);
	shell_print(sh, "tapped %s", argv[1]);
	return 0;
}

static int cmd_down(const struct shell *sh, size_t argc, char **argv)
{
	SDL_Scancode sc;

	if (argc < 2 || arg_key(sh, argv[1], &sc)) {
		return -EINVAL;
	}
	submit_key(sc, true);
	shell_print(sh, "holding %s (`minivmac up %s` to release)", argv[1], argv[1]);
	return 0;
}

static int cmd_up(const struct shell *sh, size_t argc, char **argv)
{
	SDL_Scancode sc;

	if (argc < 2 || arg_key(sh, argv[1], &sc)) {
		return -EINVAL;
	}
	submit_key(sc, false);
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
	submit_key(sc, true);
	k_msleep(ms);
	submit_key(sc, false);
	shell_print(sh, "held %s for %d ms", argv[1], ms);
	return 0;
}

static int cmd_mouse(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		return -EINVAL;
	}

	int x = atoi(argv[1]);
	int y = atoi(argv[2]);

	submit_motion(x, y);
	shell_print(sh, "mouse -> (%d, %d)", clampi(x, 0, MAC_W - 1),
		    clampi(y, 0, MAC_H - 1));
	return 0;
}

static int cmd_moverel(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		return -EINVAL;
	}

	int x, y;

	(void)SDL_GetMouseState(&x, &y);
	x += atoi(argv[1]);
	y += atoi(argv[2]);
	submit_motion(x, y);
	shell_print(sh, "mouse -> (%d, %d)", clampi(x, 0, MAC_W - 1),
		    clampi(y, 0, MAC_H - 1));
	return 0;
}

static int cmd_click(const struct shell *sh, size_t argc, char **argv)
{
	int x, y;

	if (argc >= 3) {
		x = atoi(argv[1]);
		y = atoi(argv[2]);
	} else {
		(void)SDL_GetMouseState(&x, &y);
	}
	submit_button(x, y, true);
	k_msleep(80);
	submit_button(x, y, false);
	shell_print(sh, "clicked at (%d, %d)", clampi(x, 0, MAC_W - 1),
		    clampi(y, 0, MAC_H - 1));
	return 0;
}

static int cmd_keys(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "left right up down  return/enter  esc  space  tab  "
			"backspace  shift  ctrl  alt/option  cmd/gui  a..z  0..9");
	shell_print(sh, "Mac: cmd = Command key (menu shortcuts), click selects, "
			"mouse/click coords are 0..511 x 0..341");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_minivmac,
	SHELL_CMD_ARG(key,     NULL, "<name>  -- tap (down+up), e.g. `minivmac key return`", cmd_key, 2, 0),
	SHELL_CMD_ARG(down,    NULL, "<name>  -- press and hold", cmd_down, 2, 0),
	SHELL_CMD_ARG(up,      NULL, "<name>  -- release a held key", cmd_up, 2, 0),
	SHELL_CMD_ARG(tap,     NULL, "<name> [ms]  -- hold then release (default 250 ms)", cmd_tap, 2, 1),
	SHELL_CMD_ARG(mouse,   NULL, "<x> <y>  -- move cursor to absolute coords", cmd_mouse, 3, 0),
	SHELL_CMD_ARG(moverel, NULL, "<dx> <dy>  -- move cursor by a delta", cmd_moverel, 3, 0),
	SHELL_CMD_ARG(click,   NULL, "[x y]  -- click at coords or current spot", cmd_click, 1, 2),
	SHELL_CMD(keys,        NULL, "list key names", cmd_keys),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(minivmac, &sub_minivmac, "PiZZa Mini vMac input control (keyboard + mouse)", NULL);
