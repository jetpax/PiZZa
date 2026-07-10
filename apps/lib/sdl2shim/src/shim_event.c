/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- event queue and keyboard state.
 *
 * Producer seam (work-order §5b): every source submits through
 * s2s_event_submit() -- SDLPoP's own SDL_PushEvent (timer callbacks,
 * possibly from ISR context), the scripted input source (phase 4),
 * and the HOGP keyboard client later. The queue is a spinlock-guarded
 * ring so ISR producers are safe; the game consumes from the main
 * thread via SDL_PollEvent.
 *
 * KEYDOWN/KEYUP submissions update the persistent keyboard-state
 * array the game reads through SDL_GetKeyboardState().
 */

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(s2s_event, CONFIG_SDL2SHIM_LOG_LEVEL);

#define S2S_EVENT_QUEUE_LEN 64

static SDL_Event queue[S2S_EVENT_QUEUE_LEN];
static uint32_t q_head;   /* next slot to read  */
static uint32_t q_count;  /* occupied slots     */
static struct k_spinlock q_lock;

static Uint8 key_state[SDL_NUM_SCANCODES];

/* Mouse state tracked from submitted events. Mini vMac reads the absolute
 * position both from the motion/button events AND by polling
 * SDL_GetMouseState every tick (its CheckMouseState), so GetMouseState must
 * return the same tracked position or the poll would snap the cursor back.
 */
static int mouse_x;
static int mouse_y;
static Uint32 mouse_buttons;

/* SDL2 default scancode -> keycode: letters/digits and a few printable
 * keys map to ASCII, everything else is scancode | SDLK_SCANCODE_MASK.
 * Matches the SDLK_* values in SDL.h that the games compare against
 * (Doom's i_input.c switches on event.key.keysym.sym).
 */
static SDL_Keycode keycode_from_scancode(SDL_Scancode sc)
{
	if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
		return (SDL_Keycode)('a' + (sc - SDL_SCANCODE_A));
	}
	if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
		return (SDL_Keycode)('1' + (sc - SDL_SCANCODE_1));
	}
	switch (sc) {
	case SDL_SCANCODE_0:            return '0';
	case SDL_SCANCODE_RETURN:       return '\r';
	case SDL_SCANCODE_ESCAPE:       return '\x1b';
	case SDL_SCANCODE_BACKSPACE:    return '\b';
	case SDL_SCANCODE_TAB:          return '\t';
	case SDL_SCANCODE_SPACE:        return ' ';
	case SDL_SCANCODE_MINUS:        return '-';
	case SDL_SCANCODE_EQUALS:       return '=';
	case SDL_SCANCODE_LEFTBRACKET:  return '[';
	case SDL_SCANCODE_RIGHTBRACKET: return ']';
	case SDL_SCANCODE_BACKSLASH:    return '\\';
	case SDL_SCANCODE_SEMICOLON:    return ';';
	case SDL_SCANCODE_APOSTROPHE:   return '\'';
	case SDL_SCANCODE_GRAVE:        return '`';
	case SDL_SCANCODE_COMMA:        return ',';
	case SDL_SCANCODE_PERIOD:       return '.';
	case SDL_SCANCODE_SLASH:        return '/';
	default:                        return (SDL_Keycode)(sc | SDLK_SCANCODE_MASK);
	}
}

int s2s_event_submit(const SDL_Event *event)
{
	k_spinlock_key_t key = k_spin_lock(&q_lock);
	SDL_Event ev = *event;

	if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
		SDL_Scancode sc = ev.key.keysym.scancode;

		if (sc > 0 && sc < SDL_NUM_SCANCODES) {
			key_state[sc] = (ev.type == SDL_KEYDOWN) ? 1 : 0;
		}

		/* Fill keysym.mod from the current held-modifier state so
		 * every producer (scripted, shell, future HOGP keyboard)
		 * gets correct Shift/Ctrl/Alt combos for free -- the game
		 * reads event.key.keysym.mod for e.g. Alt+Enter.
		 */
		Uint16 mod = 0;

		if (key_state[SDL_SCANCODE_LSHIFT]) { mod |= KMOD_LSHIFT; }
		if (key_state[SDL_SCANCODE_RSHIFT]) { mod |= KMOD_RSHIFT; }
		if (key_state[SDL_SCANCODE_LCTRL])  { mod |= KMOD_LCTRL; }
		if (key_state[SDL_SCANCODE_RCTRL])  { mod |= KMOD_RCTRL; }
		if (key_state[SDL_SCANCODE_LALT])   { mod |= KMOD_LALT; }
		if (key_state[SDL_SCANCODE_RALT])   { mod |= KMOD_RALT; }
		ev.key.keysym.mod = mod;

		/* Producers set .scancode; derive .sym so Doom's i_input.c
		 * (which switches on keysym.sym) works without each producer
		 * having to know the keycode mapping.
		 */
		ev.key.keysym.sym = keycode_from_scancode(sc);
	} else if (ev.type == SDL_MOUSEMOTION) {
		mouse_x = ev.motion.x;
		mouse_y = ev.motion.y;
	} else if (ev.type == SDL_MOUSEBUTTONDOWN) {
		mouse_x = ev.button.x;
		mouse_y = ev.button.y;
		mouse_buttons |= (Uint32)1u << (ev.button.button - 1);
	} else if (ev.type == SDL_MOUSEBUTTONUP) {
		mouse_x = ev.button.x;
		mouse_y = ev.button.y;
		mouse_buttons &= ~((Uint32)1u << (ev.button.button - 1));
	}

	if (q_count == S2S_EVENT_QUEUE_LEN) {
		k_spin_unlock(&q_lock, key);
		LOG_WRN("event queue full, dropping type 0x%x", ev.type);
		return -1;
	}

	queue[(q_head + q_count) % S2S_EVENT_QUEUE_LEN] = ev;
	q_count++;
	k_spin_unlock(&q_lock, key);
	return 0;
}

int SDL_PushEvent(SDL_Event *event)
{
	return (s2s_event_submit(event) == 0) ? 1 : 0;
}

int SDL_PollEvent(SDL_Event *event)
{
	k_spinlock_key_t key = k_spin_lock(&q_lock);

	if (q_count == 0) {
		k_spin_unlock(&q_lock, key);
		return 0;
	}

	if (event != NULL) {
		*event = queue[q_head];
	}
	q_head = (q_head + 1) % S2S_EVENT_QUEUE_LEN;
	q_count--;
	k_spin_unlock(&q_lock, key);
	return 1;
}

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
	if (numkeys != NULL) {
		*numkeys = SDL_NUM_SCANCODES;
	}
	return key_state;
}

Uint32 SDL_GetMouseState(int *x, int *y)
{
	if (x != NULL) {
		*x = mouse_x;
	}
	if (y != NULL) {
		*y = mouse_y;
	}
	return mouse_buttons;
}

void SDL_StartTextInput(void)
{
}

void SDL_StopTextInput(void)
{
}

void SDL_SetTextInputRect(const SDL_Rect *rect)
{
	ARG_UNUSED(rect);
}

int SDL_ShowCursor(int toggle)
{
	ARG_UNUSED(toggle);
	return SDL_DISABLE;
}

int SDL_WaitEvent(SDL_Event *event)
{
	/* Block until an event is available. Mini vMac's WaitForTheNextEvent
	 * uses this on its idle path; poll the queue with a short sleep so
	 * producer sources (shell, scripted, future HOGP keyboard) get through.
	 */
	while (SDL_PollEvent(event) == 0) {
		k_msleep(1);
	}
	return 1;
}

void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y)
{
	/* No host cursor to warp; the Mac tracks its own pointer. Mini vMac
	 * warps only to recenter under a relative-mode grab we do not use.
	 */
	ARG_UNUSED(window);
	ARG_UNUSED(x);
	ARG_UNUSED(y);
}
