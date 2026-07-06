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
		*x = 0;
	}
	if (y != NULL) {
		*y = 0;
	}
	return 0;
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
