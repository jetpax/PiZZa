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

#ifdef CONFIG_SDLPOP_AUDIO_HDMI

#include <zephyr/drivers/dma/dma_bcm2835.h>
#include "audio/hdmi_audio.h"

/* HDMI-audio bring-up diagnostics (work order M2): pump/ring
 * counters, live MAI state, recovered clocks, and the raw DMA
 * channel registers -- everything needed to localize a silent-TV
 * fault to feed vs pacing vs MAI vs regen without guessing.
 */
static int cmd_audio(const struct shell *sh, size_t argc, char **argv)
{
	struct pop_audio_hdmi_stats stats;
	struct hdmi_audio_hw_debug dbg;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	pop_audio_hdmi_get_stats(&stats);
	shell_print(sh, "state: %s%s%s  dma ch %d",
		    stats.opened ? "open" : "closed",
		    stats.tone_mode ? " [test tone]" : "",
		    stats.opened ? (stats.paused ? " paused" : " playing") : "",
		    stats.dma_chan);
	shell_print(sh, "blocks played %u (%u ms), underruns %u, dma errors %u",
		    stats.blocks_played, stats.blocks_played * 4,
		    stats.underruns, stats.dma_errors);

	hdmi_audio_hw_get_debug(&dbg);
	shell_print(sh, "clocks: hsm %u Hz, pixel %u Hz", dbg.hsm_hz,
		    dbg.pixel_hz);
	shell_print(sh, "MAI: SMP %u/%u  N %u  CTS %u  CTL %08x",
		    dbg.smp_n, dbg.smp_m, dbg.crp_n, dbg.cts, dbg.mai_ctl);

	if (stats.dma_chan >= 0) {
		const struct device *dma =
			DEVICE_DT_GET(DT_NODELABEL(dma));
		struct dma_bcm2835_chan_state cs;

		if (dma_bcm2835_get_chan_state(dma, (uint32_t)stats.dma_chan,
					       &cs) == 0) {
			shell_print(sh, "DMA: CS %08x CONBLK %08x TI %08x",
				    cs.cs, cs.conblk_ad, cs.ti);
			shell_print(sh, "     SRC %08x DST %08x LEN %u DEBUG %08x",
				    cs.source_ad, cs.dest_ad, cs.txfr_len,
				    cs.debug);
		}
	}

	return 0;
}

static int cmd_audio_starve(const struct shell *sh, size_t argc, char **argv)
{
	int blocks = atoi(argv[1]);

	ARG_UNUSED(argc);
	if (blocks <= 0 || blocks > 1000) {
		shell_print(sh, "blocks must be 1..1000");
		return -EINVAL;
	}

	pop_audio_hdmi_starve((unsigned int)blocks);
	shell_print(sh, "starving feeder for %d blocks (%d ms) -- expect "
		    "~%d underruns, then recovery; check `pop audio`",
		    blocks, blocks * 4, blocks > 7 ? blocks - 7 : 0);
	return 0;
}

#endif /* CONFIG_SDLPOP_AUDIO_HDMI */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pop,
	SHELL_CMD_ARG(key,  NULL, "<name>  -- tap (down+up), e.g. `pop key enter`", cmd_key, 2, 0),
	SHELL_CMD_ARG(down, NULL, "<name>  -- press and hold",  cmd_down, 2, 0),
	SHELL_CMD_ARG(up,   NULL, "<name>  -- release a held key", cmd_up, 2, 0),
	SHELL_CMD_ARG(tap,  NULL, "<name> [ms]  -- hold then release (default 250 ms)", cmd_tap, 2, 1),
	SHELL_CMD(keys,     NULL, "list key names", cmd_keys),
#ifdef CONFIG_SDLPOP_AUDIO_HDMI
	SHELL_CMD(audio,    NULL, "HDMI audio path diagnostics", cmd_audio),
	SHELL_CMD_ARG(starve, NULL, "<blocks>  -- starve the audio ring (underrun demo)",
		      cmd_audio_starve, 2, 0),
#endif
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(pop, &sub_pop, "PiZZa SDLPoP input control (inject keys)", NULL);
