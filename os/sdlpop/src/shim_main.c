/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDLPoP port -- Zephyr entry point.
 *
 * SDLPoP's own main() is renamed to SDL_main by the shim's SDL.h
 * (standard SDL trick); the Zephyr main thread becomes the game
 * thread. The game never returns in normal operation.
 */

#include <zephyr/kernel.h>
#include <stdio.h>

#include "pop_shim.h"

int main(void)
{
#if defined(CONFIG_SDLPOP_SIM_TUNING)
	/* Sim only: "megahit" enables cheats so the level-number arg is
	 * honored, "1" starts directly in level 1 -- skips the intro and
	 * title sequence, which TCG renders at ~1/80 real time. The
	 * scripted input then moves the Prince within seconds of boot.
	 */
	static char *argv[] = { "prince", "megahit", "1", NULL };
	int argc = 3;
#else
	static char *argv[] = { "prince", NULL };
	int argc = 1;
#endif

	printf("[sdlpop] PiZZa SDLPoP port, board %s\n", CONFIG_BOARD);

	if (IS_ENABLED(CONFIG_SDLPOP_VIDEO_SELFTEST)) {
		pop_video_selftest();
	}

	if (IS_ENABLED(CONFIG_SDLPOP_SCRIPTED_INPUT)) {
		pop_scripted_input_start();
	}

	printf("[sdlpop] starting game\n");

	int rc = SDL_main(argc, argv);

	printf("[sdlpop] game exited: %d\n", rc);
	return rc;
}
