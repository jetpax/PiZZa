/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- Zephyr entry point.
 *
 * Mini vMac's OSGLUSDL.c defines main(). Because it pulls in <SDL2/SDL.h>
 * (via cfg/CNFUIOSG.h) WITHOUT SDL_MAIN_HANDLED, the shim header's
 * `#define main SDL_main` redirects that main() to SDL_main automatically
 * -- no per-file -Dmain=SDL_main needed (unlike Doom's i_main.c, which did
 * not include SDL.h). This file includes sdl2shim.h, where SDL_MAIN_HANDLED
 * IS set, so its own main() stays main(); it builds argv and calls SDL_main.
 *
 * argv passes "boot.dsk": ScanCommandLine inserts it via Sony_Insert1, and
 * minivmac_assets.c serves it read-write from a RAM overlay, so the Mac boots
 * System 6.0.8 to the Finder. The ROM still loads via fopen("vMac.ROM").
 */

#include <zephyr/kernel.h>
#include <stdio.h>

#include "sdl2shim.h"

extern int SDL_main(int argc, char *argv[]);

int main(void)
{
	static char *argv[] = {
		"minivmac",
		"boot.dsk",
		NULL,
	};
	int argc = (int)(ARRAY_SIZE(argv) - 1);

	printf("[minivmac] PiZZa Mini vMac (Mac Plus) port, board %s\n", CONFIG_BOARD);

	if (IS_ENABLED(CONFIG_MINIVMAC_SCRIPTED_INPUT)) {
		s2s_scripted_input_start();
	}

	printf("[minivmac] entering SDL_main (ProgramMain)\n");

	int rc = SDL_main(argc, argv);

	printf("[minivmac] emulator exited: %d\n", rc);
	return rc;
}
