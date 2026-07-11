/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban port -- Zephyr entry point.
 *
 * The game's own main() is renamed to SDL_main by the shim's SDL.h
 * (standard SDL trick); the Zephyr main thread becomes the game
 * thread. The game only returns on ESC/quit.
 */

#include <zephyr/kernel.h>
#include <stdio.h>

#include "sdl2shim.h"

#ifdef CONFIG_BTINPUT
void sokoban_btinput_start(void);
#endif

int main(void)
{
	static char *argv[] = { "sokoban", NULL };

	printf("[sokoban] PiZZa sokoban port, board %s\n", CONFIG_BOARD);

	if (IS_ENABLED(CONFIG_SOKOBAN_SCRIPTED_INPUT)) {
		s2s_scripted_input_start();
	}

#ifdef CONFIG_BTINPUT
	sokoban_btinput_start();
#endif

	/* The game quits on ESC, but an appliance has nothing to exit to --
	 * relaunch at the title screen instead. App::ShutDown tears the
	 * renderer/window down cleanly, so a fresh SDL_main re-creates them.
	 */
	for (;;) {
		int rc = SDL_main(1, argv);

		printf("[sokoban] game exited: %d -- relaunching\n", rc);
		k_msleep(1000);
	}
	return 0;
}
