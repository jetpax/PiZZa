/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa DOSBox-X -- scripted keyboard input for sim verification
 * (CONFIG_DOSBOX_SCRIPTED_INPUT). Armed at the AT_PROMPT VM event;
 * dbx_scripted_pump() (called from GFX_Events) then drips one
 * keystroke every SCRIPT_KEY_MS through the real event path:
 * SDL_PushEvent -> MAPPER_CheckEvent -> KEYBOARD_AddKey -> INT9/INT16
 * -> COMMAND.COM. Pacing keeps the 16-slot BIOS keyboard buffer happy
 * while commands execute.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "SDL.h"

#define SCRIPT_KEY_MS   100
#define SCRIPT_CMD_MS   4000 /* settle time after Enter (command runs) */
#define SCRIPT_START_MS 1500

/* P2d go/no-go: mount the baked DOOM HDD, launch DOOM.EXE (DOS/4GW,
 * protected mode -> exercises the dynrec) as a timedemo. -timedemo
 * replays demo1 as fast as possible then prints the timing to the DOS
 * console and exits -- a strong dynrec correctness check (any codegen
 * bug desyncs the demo).
 *
 * With BT input in the image (CONFIG_BTINPUT) the build exists to be
 * PLAYED: launch DOOM interactive instead. The pad's remapped keys
 * cannot type at the DOS prompt, so the script still does the mount +
 * launch; the human takes over at the title screen.
 */
static const char script[] =
	"imgmount c /doom.img -t hdd -fs fat\n"
	"c:\n"
	"dir\n"
#ifdef CONFIG_BTINPUT
	"doom\n";
#else
	"doom -timedemo demo1\n";
#endif

static bool armed;
static Uint32 t_armed;
static Uint32 t_last;
static size_t s_pos;

static SDL_Scancode char_scancode(char c)
{
	if (c >= 'a' && c <= 'z') {
		return (SDL_Scancode)(SDL_SCANCODE_A + (c - 'a'));
	}
	if (c >= '1' && c <= '9') {
		return (SDL_Scancode)(SDL_SCANCODE_1 + (c - '1'));
	}
	switch (c) {
	case '0': return SDL_SCANCODE_0;
	case ' ': return SDL_SCANCODE_SPACE;
	case '\n': return SDL_SCANCODE_RETURN;
	case '.': return SDL_SCANCODE_PERIOD;
	case '/': return SDL_SCANCODE_SLASH;
	case '-': return SDL_SCANCODE_MINUS;
	case ':': return SDL_SCANCODE_SEMICOLON; /* shifted below */
	default: return SDL_SCANCODE_UNKNOWN;
	}
}

static void push_key(SDL_Scancode sc, bool down)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.scancode = sc;
	ev.key.keysym.sym = SDL_GetKeyFromScancode(sc);
	SDL_PushEvent(&ev);
}

/* ':' needs shift+; -- wrap the pair in LSHIFT make/break */
static void type_char(char c)
{
	SDL_Scancode sc = char_scancode(c);

	if (sc == SDL_SCANCODE_UNKNOWN) {
		return;
	}
	if (c == ':') {
		push_key(SDL_SCANCODE_LSHIFT, true);
	}
	push_key(sc, true);
	push_key(sc, false);
	if (c == ':') {
		push_key(SDL_SCANCODE_LSHIFT, false);
	}
}

/* called from VM_Boot_DOSBox_Kernel at the AT_PROMPT VM event */
void dbx_scripted_at_prompt(void)
{
	if (armed) {
		return;
	}
	armed = true;
	t_armed = SDL_GetTicks();
	t_last = 0;
	printk("[dbx] scripted input armed\n");
}

/* called from GFX_Events on every pump */
void dbx_scripted_pump(void)
{
	if (!armed || script[s_pos] == '\0') {
		return;
	}

	Uint32 now = SDL_GetTicks();

	if (now - t_armed < SCRIPT_START_MS) {
		return;
	}

	Uint32 gap = (s_pos > 0 && script[s_pos - 1] == '\n') ? SCRIPT_CMD_MS
							       : SCRIPT_KEY_MS;

	if (t_last != 0 && now - t_last < gap) {
		return;
	}
	t_last = now;
	type_char(script[s_pos]);
	s_pos++;
	if (script[s_pos] == '\0') {
		printk("[dbx] scripted input complete\n");
	}
}
