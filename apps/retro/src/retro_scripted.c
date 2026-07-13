/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- scripted synthetic input
 * (CONFIG_RETRO_SCRIPTED_INPUT, qemu sim gate).
 *
 * Two timelines:
 *  - CONFIG_RETRO_MENU: drive the launcher -- launch the first core,
 *    play, L+R back to the menu, navigate to the second, launch, play.
 *    Exercises the fs scan + in-process swap in sim.
 *  - single-core: START then arrows (the 2048 opening).
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

#if defined(CONFIG_RETRO_SCRIPTED_CONTENT)
/* M5 content-core gate: the seeded content core is written first, so it
 * is menu entry 0. Launch it, then pick content entry 0 (the one seeded
 * WAD) in the browser, then hold while it reads the file off the fs and
 * renders. Exercises browse -> retro_game_info.path -> the core's
 * fopen/fread over exported fs_*.
 */
static void script_main(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(3000);
	printf("[retro] scripted(content): launch core 0\n");
	press(SDL_SCANCODE_X, 150); /* A = launch highlighted (entry 0) */

	k_msleep(2500);
	printf("[retro] scripted(content): pick content 0\n");
	press(SDL_SCANCODE_X, 150); /* A = load the highlighted WAD */

	k_msleep(20000); /* let the core read + render its verdict frames */
	printf("[retro] scripted(content): timeline done\n");
}
#elif defined(CONFIG_RETRO_MENU)
/* The L+R return-to-menu chord: both shoulders (Q/W in the frontend
 * map) held together.
 */
static void press_menu_chord(void)
{
	submit_key(SDL_SCANCODE_Q, SDL_KEYDOWN);
	submit_key(SDL_SCANCODE_W, SDL_KEYDOWN);
	k_msleep(300);
	submit_key(SDL_SCANCODE_Q, SDL_KEYUP);
	submit_key(SDL_SCANCODE_W, SDL_KEYUP);
}

static void play_a_bit(void)
{
	static const SDL_Scancode dirs[] = {
		SDL_SCANCODE_UP, SDL_SCANCODE_LEFT,
		SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT,
	};

	/* Cover both games' "start": 2048 wants retropad START (RETURN);
	 * sokoban wants retropad X, which is SDL_SCANCODE_S in the
	 * frontend map (-> the core's 'O' = Original levels).
	 */
	press(SDL_SCANCODE_RETURN, 120);
	k_msleep(300);
	press(SDL_SCANCODE_S, 120);
	k_msleep(600);

	for (int i = 0; i < 8; i++) {
		press(dirs[i % 4], 100);
		k_msleep(400);
	}
}

static void script_main(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(3000);
	printf("[retro] scripted: launch first core\n");
	press(SDL_SCANCODE_X, 150); /* A = launch highlighted (entry 0) */
	k_msleep(5000);
	play_a_bit();

	printf("[retro] scripted: L+R back to launcher\n");
	press_menu_chord();
	k_msleep(3000);

	printf("[retro] scripted: pick + launch second core\n");
	press(SDL_SCANCODE_DOWN, 150); /* select entry 1 */
	k_msleep(500);
	press(SDL_SCANCODE_X, 150);     /* launch */
	k_msleep(5000);
	play_a_bit();

	printf("[retro] scripted: L+R back to launcher (done)\n");
	press_menu_chord();
}
#else
static void script_main(void *a, void *b, void *c)
{
	static const SDL_Scancode dirs[] = {
		SDL_SCANCODE_UP, SDL_SCANCODE_LEFT,
		SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT,
	};

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(2000);
	printf("[retro] scripted: START (title -> game)\n");
	press(SDL_SCANCODE_RETURN, 150);
	k_msleep(400);

	/* Retropad X (scancode S in the frontend map): starts a game on
	 * cores whose action button is X (sokoban: X -> 'O' -> Original
	 * levels). 2048 ignores it.
	 */
	press(SDL_SCANCODE_S, 150);
	k_msleep(400);

	for (int i = 0; i < 48; i++) {
		press(dirs[i % 4], 100);
		k_msleep(350);
	}
	printf("[retro] scripted: timeline done\n");
}
#endif

void s2s_scripted_input_start(void)
{
	k_thread_create(&script_thread, script_stack, SCRIPT_STACK_SIZE,
			script_main, NULL, NULL, NULL,
			K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&script_thread, "retro_script");
}
