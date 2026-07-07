/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- scripted input source (CONFIG_MINIVMAC_SCRIPTED_INPUT).
 *
 * A synthetic mouse + keyboard producer that drives the booted Finder
 * through the same sdl2shim event-queue seam the `minivmac` shell (and a
 * future BT gamepad / HOGP client) use. Runs on its own thread; once System
 * 6 reaches the desktop it moves the cursor, clicks the System Folder icon to
 * select it, then issues Cmd-A (Select All) so the whole window inverts and
 * clicks empty desktop to deselect -- a visible proof of motion + click +
 * keyboard for the qemu sim gate.
 *
 * Absolute positions: Mini vMac's OSGLU takes event.motion.x/y as absolute
 * window coords, and the window is the native 512x342 Mac screen (the HVS
 * scales on hardware), so coords map 1:1 to the Mac screen. Several rounds
 * with generous spacing ride out qemu's dilated, capture-slowed timing.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(minivmac_scripted, CONFIG_SDL2SHIM_LOG_LEVEL);

static void move_mouse(int x, int y)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = SDL_MOUSEMOTION;
	ev.motion.x = x;
	ev.motion.y = y;
	s2s_event_submit(&ev);
}

static void mouse_button(int x, int y, bool down)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.button = SDL_BUTTON_LEFT;
	ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.button.x = x;
	ev.button.y = y;
	s2s_event_submit(&ev);
}

static void click(int x, int y)
{
	mouse_button(x, y, true);
	k_msleep(120);
	mouse_button(x, y, false);
}

static void key(SDL_Scancode sc, bool down)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.keysym.scancode = sc;
	s2s_event_submit(&ev);
}

/* Positions on the 512x342 Mac screen, from the Phase-3 Finder capture: the
 * "System Folder" icon sits near the top-left of the open "System Startup"
 * window; empty desktop is lower-right.
 */
#define ICON_X   62
#define ICON_Y   98
#define EMPTY_X  400
#define EMPTY_Y  260

static void scripted_run(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* Let System 6 reach the Finder desktop before driving it. Generous:
	 * the semihost capture dilates wall-clock badly under qemu TCG.
	 */
	k_msleep(60000);

	for (int round = 0; round < 4; round++) {
		LOG_INF("scripted: round %d begins", round);

		move_mouse(256, 171);       /* cursor to centre */
		k_msleep(2500);

		move_mouse(ICON_X, ICON_Y); /* over the System Folder icon */
		k_msleep(1500);

		click(ICON_X, ICON_Y);      /* select it: the icon inverts */
		k_msleep(3000);

		key(SDL_SCANCODE_LGUI, true);   /* Cmd-A: Select All */
		k_msleep(100);
		key(SDL_SCANCODE_A, true);
		k_msleep(150);
		key(SDL_SCANCODE_A, false);
		k_msleep(100);
		key(SDL_SCANCODE_LGUI, false);
		k_msleep(3000);

		click(EMPTY_X, EMPTY_Y);    /* empty desktop: deselect */
		k_msleep(3000);
	}

	LOG_INF("scripted: timeline complete");
}

static K_THREAD_STACK_DEFINE(scripted_stack, 2048);
static struct k_thread scripted_thread;

void s2s_scripted_input_start(void)
{
	k_thread_create(&scripted_thread, scripted_stack,
			K_THREAD_STACK_SIZEOF(scripted_stack),
			scripted_run, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&scripted_thread, "minivmac_scripted");
}
