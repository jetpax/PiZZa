/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- scripted synthetic input
 * (CONFIG_RETRO_SCRIPTED_INPUT, qemu sim gate).
 *
 * 2048 timeline: the title screen advances on a START *release* edge
 * (game_shared.c handle_input), then tiles move on arrow press edges.
 * Alternating directions guarantees board movement regardless of tile
 * spawns.
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>

#include "sdl2shim.h"

#define SCRIPT_STACK_SIZE 4096

static K_THREAD_STACK_DEFINE(script_stack, SCRIPT_STACK_SIZE);
static struct k_thread script_thread;

static void submit_key(SDL_Scancode sc, Uint32 type)
{
	SDL_Event e;

	memset(&e, 0, sizeof(e));
	e.type = type;
	e.key.keysym.scancode = sc;
	s2s_event_submit(&e);
}

static void press(SDL_Scancode sc, int hold_ms)
{
	submit_key(sc, SDL_KEYDOWN);
	k_msleep(hold_ms);
	submit_key(sc, SDL_KEYUP);
}

static void script_main(void *a, void *b, void *c)
{
	static const SDL_Scancode dirs[] = {
		SDL_SCANCODE_UP,
		SDL_SCANCODE_LEFT,
		SDL_SCANCODE_DOWN,
		SDL_SCANCODE_RIGHT,
	};

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(2000);
	printf("[retro] scripted: START (title -> game)\n");
	press(SDL_SCANCODE_RETURN, 150);
	k_msleep(400);

	for (int i = 0; i < 48; i++) {
		press(dirs[i % 4], 100);
		k_msleep(350);
	}
	printf("[retro] scripted: timeline done\n");
}

void s2s_scripted_input_start(void)
{
	k_thread_create(&script_thread, script_stack, SCRIPT_STACK_SIZE,
			script_main, NULL, NULL, NULL,
			K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&script_thread, "retro_script");
}
