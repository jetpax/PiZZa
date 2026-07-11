/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban -- scripted synthetic input (CONFIG_SOKOBAN_SCRIPTED_INPUT).
 *
 * Drives the sim gate on qemu: hold on the title screen (frames
 * captured), press O to start the Original levels, then walk the only
 * legal opening of level 1 -- the man starts at tile (11,8) walled
 * left/right/down, so UP, then LEFT twice along the open corridor.
 * Each press is a KEYDOWN, a hold long enough to span a frame, and a
 * KEYUP; the game latches held keys once per frame and a started move
 * completes on its own.
 */

#include <zephyr/kernel.h>

#include "sdl2shim.h"

#define SCRIPT_STACK_SIZE 2048

static K_THREAD_STACK_DEFINE(script_stack, SCRIPT_STACK_SIZE);
static struct k_thread script_thread;

static void press(SDL_Scancode sc, int hold_ms)
{
	SDL_Event ev = { 0 };

	ev.type = SDL_KEYDOWN;
	ev.key.state = SDL_PRESSED;
	ev.key.keysym.scancode = sc;
	s2s_event_submit(&ev);

	k_msleep(hold_ms);

	ev.type = SDL_KEYUP;
	ev.key.state = SDL_RELEASED;
	s2s_event_submit(&ev);
}

static void script_run(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* title screen renders + gets captured */
	k_msleep(3000);
	press(SDL_SCANCODE_O, 150);

	/* level 1 loads + renders */
	k_msleep(2000);
	press(SDL_SCANCODE_UP, 150);
	k_msleep(1500);
	press(SDL_SCANCODE_LEFT, 150);
	k_msleep(1500);
	press(SDL_SCANCODE_LEFT, 150);

	/* final state renders + gets captured */
	k_msleep(3000);
	printk("[sokoban] scripted input done\n");
}

void s2s_scripted_input_start(void)
{
	k_thread_create(&script_thread, script_stack, SCRIPT_STACK_SIZE,
			script_run, NULL, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&script_thread, "sok_script");
}
