/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Doom port -- `doom` shell command set (USB CDC ACM).
 *
 * Injects key events into the game through the same shim_event.c
 * producer seam the scripted source and the future HOGP keyboard
 * client use (work-order §5b). This is the interactive control path
 * on hardware until the Bluetooth keyboard lands -- you drive Doom
 * over the serial line.
 *
 *   doom key <name>       tap: keydown + keyup (menus, "press any key")
 *   doom down <name>      press and HOLD (movement -- left/right/up/shift)
 *   doom up <name>        release a held key
 *   doom tap <name> [ms]  hold <name> for ms (default 250) then release
 *   doom keys             list key names
 *
 * <name>: left right up down  return|enter  esc  space  shift  ctrl
 *         a..z  and the digits 0..9.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp (picolibc keeps it here) */

#include "sdl2shim.h"

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
	s2s_event_submit(&ev);
}

static int arg_key(const struct shell *sh, char *arg, SDL_Scancode *out)
{
	SDL_Scancode sc = lookup(arg);

	if (sc == SDL_SCANCODE_UNKNOWN) {
		shell_error(sh, "unknown key '%s' (try `doom keys`)", arg);
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
	shell_print(sh, "holding %s (doom up %s to release)", argv[1], argv[1]);
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
	shell_print(sh, "Doom: arrows move/turn, ctrl = fire, space = "
			"use/open, alt = strafe, esc = menu, enter = confirm");
	return 0;
}

#ifdef CONFIG_SDL2SHIM_AUDIO_HDMI

#include <zephyr/drivers/dma/dma_bcm2835.h>
#include <math.h>
#include <SDL2/SDL_mixer.h>
#include "audio/hdmi_audio.h"

/* HDMI-audio bring-up diagnostics (work order M2): pump/ring
 * counters, live MAI state, recovered clocks, and the raw DMA
 * channel registers -- everything needed to localize a silent-TV
 * fault to feed vs pacing vs MAI vs regen without guessing.
 */
static int cmd_audio(const struct shell *sh, size_t argc, char **argv)
{
	struct s2s_audio_hdmi_stats stats;
	struct hdmi_audio_hw_debug dbg;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	s2s_audio_hdmi_get_stats(&stats);
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

	s2s_audio_hdmi_starve((unsigned int)blocks);
	shell_print(sh, "starving feeder for %d blocks (%d ms) -- expect "
		    "~%d underruns, then recovery; check `doom audio`",
		    blocks, blocks * 4, blocks > 7 ? blocks - 7 : 0);
	return 0;
}

/* Mixer state: is it open, at what rate, and are Doom's sounds actually
 * reaching it (channels playing > 0 during gameplay)?
 */
extern uint32_t mixer_play_calls;
extern uint32_t mixer_last_alen;
extern int mixer_last_chan;
extern uint32_t mixer_finishes;

/* Doom WAD accessors, declared here to probe a sound lump the way
 * CacheSFX does without pulling in Doom's headers (which clash).
 */
extern int W_CheckNumForName(char *name);
extern int W_LumpLength(unsigned int lump);
extern void *W_CacheLumpNum(int lump, int tag);

static int cmd_mix(const struct shell *sh, size_t argc, char **argv)
{
	int freq = 0, chans = 0;
	Uint16 fmt = 0;
	int is_open;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	is_open = Mix_QuerySpec(&freq, &fmt, &chans);
	shell_print(sh, "mixer: %s, %d Hz, fmt 0x%04x, %d ch",
		    is_open ? "open" : "closed", freq, fmt, chans);
	shell_print(sh, "channels currently playing: %d", Mix_Playing(-1));
	shell_print(sh, "Mix_PlayChannelTimed: %u calls (incl. mixtest), last chan %d, "
		    "last alen %u B = %u frames; instant-finishes %u",
		    mixer_play_calls, mixer_last_chan, mixer_last_alen,
		    mixer_last_alen / 4, mixer_finishes);
	return 0;
}

/* Loop a known 1 kHz sine THROUGH the mixer (Mix_PlayChannelTimed ->
 * mixer_callback -> pump -> MAI). Clean tone => the mixer path is good
 * and the game noise is in Doom's chunk data; noise here => the mixer
 * core itself is wrong on target. Best run at the title/menu (quiet).
 */
static int16_t mixtest_sine[441 * 2];
static Mix_Chunk mixtest_chunk;

static int cmd_mixtest(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < 441; i++) {
		int16_t s = (int16_t)(8192.0f * sinf(2.0f * 3.14159265f *
					1000.0f * (float)i / 44100.0f));

		mixtest_sine[2 * i] = s;
		mixtest_sine[2 * i + 1] = s;
	}
	mixtest_chunk.allocated = 0;
	mixtest_chunk.abuf = (Uint8 *)mixtest_sine;
	mixtest_chunk.alen = sizeof(mixtest_sine);
	mixtest_chunk.volume = 128;

	int ch = Mix_PlayChannelTimed(0, &mixtest_chunk, -1, -1);

	Mix_SetPanning(0, 255, 255);
	shell_print(sh, "mixtest: 1 kHz sine looping on channel %d via the mixer", ch);
	shell_print(sh, "  clean tone => mixer path OK (fault is Doom's data)");
	shell_print(sh, "  noise      => mixer core wrong on target; `doom halt` to stop");
	return 0;
}

static int cmd_halt(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	Mix_HaltChannel(-1);
	shell_print(sh, "halted all mixer channels");
	return 0;
}

/* Probe a DMX sound lump exactly as CacheSFX does: resolve dsNAME, read
 * its length and the 8-byte header. Valid DMX = hdr "03 00"; anything
 * else (or a NOT-FOUND / bad length) is why CacheSFX rejects the sound.
 */
static int cmd_sfxprobe(const struct shell *sh, size_t argc, char **argv)
{
	char lump[16] = "ds";
	const uint8_t *d;
	int n, len;

	strncat(lump, (argc >= 2) ? argv[1] : "pistol", sizeof(lump) - 3);

	n = W_CheckNumForName(lump);
	if (n < 0) {
		shell_print(sh, "%s: lump NOT FOUND", lump);
		return 0;
	}
	len = W_LumpLength((unsigned int)n);
	d = W_CacheLumpNum(n, 1 /* PU_STATIC */);
	shell_print(sh, "%s: lump %d, len %d", lump, n, len);
	shell_print(sh, "  hdr %02x %02x  rate %u  len32 %u  (valid DMX = 03 00)",
		    d[0], d[1], (unsigned int)(d[2] | (d[3] << 8)),
		    (unsigned int)(d[4] | (d[5] << 8) | (d[6] << 16) |
				   ((unsigned int)d[7] << 24)));
	return 0;
}

#endif /* CONFIG_SDL2SHIM_AUDIO_HDMI */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_doom,
	SHELL_CMD_ARG(key,  NULL, "<name>  -- tap (down+up), e.g. `doom key enter`", cmd_key, 2, 0),
	SHELL_CMD_ARG(down, NULL, "<name>  -- press and hold",  cmd_down, 2, 0),
	SHELL_CMD_ARG(up,   NULL, "<name>  -- release a held key", cmd_up, 2, 0),
	SHELL_CMD_ARG(tap,  NULL, "<name> [ms]  -- hold then release (default 250 ms)", cmd_tap, 2, 1),
	SHELL_CMD(keys,     NULL, "list key names", cmd_keys),
#ifdef CONFIG_SDL2SHIM_AUDIO_HDMI
	SHELL_CMD(audio,    NULL, "HDMI audio path diagnostics", cmd_audio),
	SHELL_CMD_ARG(starve, NULL, "<blocks>  -- starve the audio ring (underrun demo)",
		      cmd_audio_starve, 2, 0),
	SHELL_CMD(mix,      NULL, "mixer state (freq, channels playing)", cmd_mix),
	SHELL_CMD(mixtest,  NULL, "loop a 1 kHz sine through the mixer (isolate mixer vs data)", cmd_mixtest),
	SHELL_CMD(halt,     NULL, "halt all mixer channels", cmd_halt),
	SHELL_CMD_ARG(sfxprobe, NULL, "[name]  -- read a DMX sound lump (default pistol)", cmd_sfxprobe, 1, 1),
#endif
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(doom, &sub_doom, "PiZZa Doom input control (inject keys)", NULL);
