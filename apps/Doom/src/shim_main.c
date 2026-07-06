/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- Zephyr entry point.
 *
 * sdl2-doom's i_main.c main() is renamed to SDL_main by a per-file
 * -Dmain=SDL_main (i_main.c does not include <SDL2/SDL.h>, so the
 * header's own main->SDL_main redirect cannot reach it). This main()
 * builds argv and calls it.
 *
 * The IWAD is the rodata-baked WAD the fd layer (doom_assets.c)
 * resolves at "/doom1.wad"; -iwad points Doom straight at it so d_iwad
 * skips its filesystem search.
 */

#include <zephyr/kernel.h>
#include <stdio.h>

#include "sdl2shim.h"

extern int SDL_main(int argc, char *argv[]);

int main(void)
{
	static char *argv[] = {
		"doom",
		"-iwad", "/doom1.wad",
#if defined(CONFIG_DOOM_SIM_TUNING)
		/* Boot straight to E1M1: the title/demo loop renders far too
		 * slowly under qemu TCG to reach interactively.
		 */
		"-warp", "1", "1",
#endif
		NULL,
	};
	int argc = (int)(ARRAY_SIZE(argv) - 1);

	printf("[doom] PiZZa sdl2-doom port, board %s\n", CONFIG_BOARD);

	if (IS_ENABLED(CONFIG_DOOM_SCRIPTED_INPUT)) {
		s2s_scripted_input_start();
	}

	printf("[doom] entering SDL_main (D_DoomMain)\n");

	int rc = SDL_main(argc, argv);

	printf("[doom] game exited: %d\n", rc);
	return rc;
}
