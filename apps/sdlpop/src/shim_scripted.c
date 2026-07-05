/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDLPoP port -- scripted input source
 * (CONFIG_SDLPOP_SCRIPTED_INPUT).
 *
 * A synthetic input producer that drives the game headless through
 * the same shim_event.c producer seam the HOGP keyboard client will
 * use later (work-order §5b). It runs on its own thread and injects
 * a timeline of key press/release events on wall-clock (k_uptime),
 * demonstrating: timing works, the event queue accepts an external
 * producer, and a scripted sequence moves the character.
 *
 * Timeline (ms from game start): dismiss the intro/title screens,
 * start level 1, then run the Prince right and jump -- a visible,
 * deterministic motion sequence for the phase-4 frame capture.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pop_shim.h"

LOG_MODULE_REGISTER(pop_scripted, CONFIG_SDLPOP_LOG_LEVEL);

struct script_step {
	uint32_t at_ms;
	SDL_Scancode scancode;
	bool down;
};

/* Under CONFIG_SDLPOP_SIM_TUNING the game boots straight into level 1
 * (megahit + level arg in shim_main.c), so no intro/title to dismiss.
 * The Prince stands facing left in the level-1 start room; tapping
 * Right turns him, holding Right runs him toward the gate; Up is a
 * standing jump. Each key: press, hold, release.
 */
static const struct script_step timeline[] = {
	/* dismiss any level-start prompt */
	{ 1500,  SDL_SCANCODE_RETURN, true },  { 1600,  SDL_SCANCODE_RETURN, false },
	/* turn + run right */
	{ 2500,  SDL_SCANCODE_RIGHT, true },   { 5000,  SDL_SCANCODE_RIGHT, false },
	/* standing jump */
	{ 5500,  SDL_SCANCODE_UP, true },      { 5900,  SDL_SCANCODE_UP, false },
	/* run right again */
	{ 6500,  SDL_SCANCODE_RIGHT, true },   { 9000,  SDL_SCANCODE_RIGHT, false },
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
	pop_event_submit(&ev);
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

void pop_scripted_input_start(void)
{
	k_thread_create(&scripted_thread, scripted_stack,
			K_THREAD_STACK_SIZEOF(scripted_stack),
			scripted_run, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&scripted_thread, "pop_scripted");
}
