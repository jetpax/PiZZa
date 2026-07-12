/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- Zephyr entry point.
 */

#include <zephyr/kernel.h>
#include <stdio.h>

#include "sdl2shim.h"
#include "retro_frontend.h"

#ifdef CONFIG_BTINPUT
void retro_btinput_start(void);
#endif

int main(void)
{
	printf("[retro] PiZZa libretro frontend, board %s\n", CONFIG_BOARD);

	if (IS_ENABLED(CONFIG_RETRO_SCRIPTED_INPUT)) {
		s2s_scripted_input_start();
	}

#ifdef CONFIG_BTINPUT
	retro_btinput_start();
#endif

	/* Only returns on a core load failure; an appliance has nothing
	 * to exit to, so retry (the failure is loud on the console).
	 */
	for (;;) {
		int rc = retro_frontend_run();

		printf("[retro] frontend exited: %d -- retrying\n", rc);
		k_msleep(2000);
	}
	return 0;
}
