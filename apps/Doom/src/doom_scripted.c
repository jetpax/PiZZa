/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- scripted input source (CONFIG_DOOM_SCRIPTED_INPUT).
 *
 * A synthetic input producer that drives Doom headless through the same
 * sdl2shim event-queue seam a HOGP keyboard client will use later. It
 * runs on its own thread and injects a wall-clock timeline of key
 * press/release events: open the menu, New Game -> episode 1 -> skill ->
 * E1M1, then move and fire in-game. Proves the menu path and in-game
 * input for the qemu sim gate (G2).
 *
 * Events queue in shim_event.c, so the sequence is preserved even under
 * qemu TCG's dilated timing; the spacing is generous so each menu screen
 * and the level load complete between keys (and so the frame capture
 * lands on distinct screens).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(doom_scripted, CONFIG_SDL2SHIM_LOG_LEVEL);

struct script_step {
	uint32_t at_ms;
	SDL_Scancode scancode;
	bool down;
};

/* Doom 1 shareware menu path: Escape opens the main menu (cursor on New
 * Game); three Returns walk New Game -> Knee-Deep in the Dead -> default
 * skill -> start E1M1 (no Nightmare confirm at the default cursor). Then
 * forward + fire + turn in-game.
 */
static const struct script_step timeline[] = {
	{ 8000,  SDL_SCANCODE_ESCAPE, true }, { 8150,  SDL_SCANCODE_ESCAPE, false },
	{ 12000, SDL_SCANCODE_RETURN, true }, { 12150, SDL_SCANCODE_RETURN, false },
	{ 16000, SDL_SCANCODE_RETURN, true }, { 16150, SDL_SCANCODE_RETURN, false },
	{ 20000, SDL_SCANCODE_RETURN, true }, { 20150, SDL_SCANCODE_RETURN, false },
	/* in-game: forward, fire, turn, forward, fire */
	{ 26000, SDL_SCANCODE_UP,    true },
	{ 30000, SDL_SCANCODE_LCTRL, true }, { 30250, SDL_SCANCODE_LCTRL, false },
	{ 32000, SDL_SCANCODE_UP,    false },
	{ 33000, SDL_SCANCODE_RIGHT, true }, { 35000, SDL_SCANCODE_RIGHT, false },
	{ 36000, SDL_SCANCODE_UP,    true }, { 40000, SDL_SCANCODE_UP,    false },
	{ 41000, SDL_SCANCODE_LCTRL, true }, { 41250, SDL_SCANCODE_LCTRL, false },
};

static K_THREAD_STACK_DEFINE(scripted_stack, 2048);
static struct k_thread scripted_thread;

static void push_key(SDL_Scancode sc, bool down)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.scancode = sc;
	s2s_event_submit(&ev);
}

static void scripted_run(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t t0 = k_uptime_get();

	for (size_t i = 0; i < ARRAY_SIZE(timeline); i++) {
		int64_t due = t0 + timeline[i].at_ms;
		int64_t now = k_uptime_get();

		if (due > now) {
			k_msleep((int32_t)(due - now));
		}
		push_key(timeline[i].scancode, timeline[i].down);
		LOG_DBG("scripted: t=%u sc=%d %s", timeline[i].at_ms,
			timeline[i].scancode, timeline[i].down ? "down" : "up");
	}
	LOG_INF("scripted: timeline complete");
}

void s2s_scripted_input_start(void)
{
	k_thread_create(&scripted_thread, scripted_stack,
			K_THREAD_STACK_SIZEOF(scripted_stack),
			scripted_run, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&scripted_thread, "doom_scripted");
}
