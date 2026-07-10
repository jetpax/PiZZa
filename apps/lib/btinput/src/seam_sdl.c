/*
 * apps/lib/btinput -- SDL seam (CONFIG_BTINPUT_SEAM_SDL).
 *
 * Translates btinput's sink events into SDL events and submits them through
 * sdl2shim's s2s_event_submit(), the same producer path the games' own
 * SDL_PushEvent uses. From there SDL_PollEvent / SDL_GetKeyboardState /
 * SDL_GetMouseState behave exactly as if a host SDL had delivered the input,
 * so a consumer game needs zero code changes.
 *
 * Keyboard: SDL2 scancodes ARE the USB HID usage numbers (the shim's SDL.h
 * documents this as an external contract), so translation is the identity
 * map plus a range check. An optional consumer remap table overrides single
 * usages -- the 8BitDo Micro's stock K-mode map is letters, which a game
 * wants remapped to arrows/ctrl/space. keysym.sym and keysym.mod are derived
 * inside s2s_event_submit(); only the scancode matters here.
 *
 * Mouse: HID reports are relative. An absolute position is integrated and
 * clamped to a consumer-set bounds box, and SDL_MOUSEMOTION carries BOTH
 * forms (x/y absolute, xrel/yrel relative) so both consumer styles work
 * (Mini vMac polls absolute via SDL_GetMouseState; DOSBox/SDLPoP read
 * relative motion). Buttons follow the boot-report bit order -> SDL button
 * numbers; wheel maps sign-for-sign onto SDL_MOUSEWHEEL.y (positive = away
 * from the user).
 *
 * Context: sinks run on the BT RX thread; s2s_event_submit is spinlock-
 * guarded and ISR-safe, so no extra locking is needed here.
 */

#include <zephyr/kernel.h>

#include <SDL2/SDL.h>

#include "sdl2shim.h"
#include "btinput.h"

/* --- keyboard ---------------------------------------------------------- */

static const struct btinput_seam_key *keymap;
static size_t keymap_len;

void btinput_seam_sdl_set_keymap(const struct btinput_seam_key *map,
				 size_t count)
{
	keymap = map;
	keymap_len = (map != NULL) ? count : 0;
}

static void seam_key(uint8_t usage, bool pressed, void *user)
{
	uint16_t sc = usage;

	ARG_UNUSED(user);

	for (size_t i = 0; i < keymap_len; i++) {
		if (keymap[i].usage == usage) {
			sc = keymap[i].scancode;
			break;
		}
	}

	if (sc == 0 || sc >= SDL_NUM_SCANCODES) {
		return;
	}

	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.scancode = (SDL_Scancode)sc;
	s2s_event_submit(&ev);
}

/* --- mouse ------------------------------------------------------------- */

static int bound_w = 640;
static int bound_h = 400;
static int abs_x = 320;
static int abs_y = 200;
static Uint32 btn_state;

void btinput_seam_sdl_set_bounds(int w, int h)
{
	if (w > 0 && h > 0) {
		bound_w = w;
		bound_h = h;
		abs_x = CLAMP(abs_x, 0, bound_w - 1);
		abs_y = CLAMP(abs_y, 0, bound_h - 1);
	}
}

static void seam_mouse_move(int dx, int dy, int wheel, void *user)
{
	SDL_Event ev;

	ARG_UNUSED(user);

	if (dx != 0 || dy != 0) {
		abs_x = CLAMP(abs_x + dx, 0, bound_w - 1);
		abs_y = CLAMP(abs_y + dy, 0, bound_h - 1);

		memset(&ev, 0, sizeof(ev));
		ev.type = SDL_MOUSEMOTION;
		ev.motion.state = btn_state;
		ev.motion.x = abs_x;
		ev.motion.y = abs_y;
		ev.motion.xrel = dx;
		ev.motion.yrel = dy;
		s2s_event_submit(&ev);
	}

	if (wheel != 0) {
		memset(&ev, 0, sizeof(ev));
		ev.type = SDL_MOUSEWHEEL;
		ev.wheel.y = wheel;
		s2s_event_submit(&ev);
	}
}

static void seam_mouse_btn(uint8_t button, bool pressed, void *user)
{
	/* Boot-report bit order -> SDL button numbers. */
	static const Uint8 sdl_btn[] = {
		SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE,
		SDL_BUTTON_X1, SDL_BUTTON_X1 + 1,
	};
	SDL_Event ev;

	ARG_UNUSED(user);

	if (button >= ARRAY_SIZE(sdl_btn)) {
		return;
	}

	Uint32 bit = (Uint32)1u << (sdl_btn[button] - 1);

	if (pressed) {
		btn_state |= bit;
	} else {
		btn_state &= ~bit;
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.button = sdl_btn[button];
	ev.button.state = pressed ? SDL_PRESSED : SDL_RELEASED;
	ev.button.clicks = 1;
	ev.button.x = abs_x;
	ev.button.y = abs_y;
	s2s_event_submit(&ev);
}

/* --- registration ------------------------------------------------------ */

void btinput_seam_sdl_attach(void)
{
	btinput_set_key_cb(seam_key, NULL);
	btinput_set_mouse_cbs(seam_mouse_btn, seam_mouse_move, NULL);
}
