/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- ticks/delay/perf counters and SDL_AddTimer.
 *
 * SDL timer callbacks run from k_timer expiry, i.e. ISR context --
 * unlike real SDL2's timer thread. SDLPoP's only callback pushes a
 * USEREVENT into the (ISR-safe) event queue and returns the same
 * interval, so this is sound. The expiry handler honors the
 * callback's returned interval per SDL semantics anyway.
 */

#include <zephyr/kernel.h>

#include "sdl2shim.h"

#define S2S_MAX_TIMERS 4

struct s2s_timer {
	struct k_timer ktimer;
	SDL_TimerCallback callback;
	void *param;
	Uint32 interval_ms;
	bool in_use;
};

static struct s2s_timer timers[S2S_MAX_TIMERS];

Uint32 SDL_GetTicks(void)
{
	return (Uint32)k_uptime_get();
}

void SDL_Delay(Uint32 ms)
{
	k_msleep((int32_t)ms);
}

Uint64 SDL_GetPerformanceCounter(void)
{
	return k_cycle_get_64();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
	return (Uint64)sys_clock_hw_cycles_per_sec();
}

static void s2s_timer_expiry(struct k_timer *ktimer)
{
	struct s2s_timer *t = CONTAINER_OF(ktimer, struct s2s_timer, ktimer);
	Uint32 next = t->callback(t->interval_ms, t->param);

	if (next == 0) {
		k_timer_stop(ktimer);
		t->in_use = false;
	} else if (next != t->interval_ms) {
		t->interval_ms = next;
		k_timer_start(ktimer, K_MSEC(next), K_MSEC(next));
	}
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param)
{
	if (callback == NULL || interval == 0) {
		s2s_set_error("AddTimer: bad args");
		return 0;
	}

	for (int i = 0; i < S2S_MAX_TIMERS; i++) {
		if (timers[i].in_use) {
			continue;
		}

		struct s2s_timer *t = &timers[i];

		t->callback = callback;
		t->param = param;
		t->interval_ms = interval;
		t->in_use = true;
		k_timer_init(&t->ktimer, s2s_timer_expiry, NULL);
		k_timer_start(&t->ktimer, K_MSEC(interval), K_MSEC(interval));
		return i + 1;   /* SDL_TimerID 0 = failure */
	}

	s2s_set_error("AddTimer: pool exhausted");
	return 0;
}

SDL_bool SDL_RemoveTimer(SDL_TimerID id)
{
	if (id < 1 || id > S2S_MAX_TIMERS || !timers[id - 1].in_use) {
		return SDL_FALSE;
	}
	k_timer_stop(&timers[id - 1].ktimer);
	timers[id - 1].in_use = false;
	return SDL_TRUE;
}
