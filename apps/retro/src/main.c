/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- Zephyr entry point.
 */

#include <zephyr/kernel.h>
#include <stdio.h>

#include "sdl2shim.h"
#include "retro_frontend.h"
#include "retro_core.h"

#ifdef CONFIG_RETRO_MENU
#include "retro_menu.h"
#endif

#ifdef CONFIG_RETRO_QEMU_RAMDISK_SEED
#include "retro_qemu_seed.h"
#endif

#ifdef CONFIG_BTINPUT
void retro_btinput_start(void);
#endif

int main(void)
{
	printf("[retro] PiZZa libretro frontend, board %s\n", CONFIG_BOARD);

#ifdef CONFIG_RETRO_QEMU_RAMDISK_SEED
	retro_qemu_seed();
#endif

	if (IS_ENABLED(CONFIG_RETRO_SCRIPTED_INPUT)) {
		s2s_scripted_input_start();
	}

#ifdef CONFIG_BTINPUT
	retro_btinput_start();
#endif

#ifdef CONFIG_RETRO_MENU
	/* Launcher: pick a core, run it until the user asks for the menu
	 * (L+R chord / `retro menu`), then reload the picker. A bind/load
	 * failure falls straight back to the menu.
	 */
	for (;;) {
		static char path[128];

		if (retro_menu_run(path, sizeof(path)) == 0) {
			retro_core_set_path(path);
			retro_frontend_run();
		} else {
			k_msleep(1000);
		}
	}
#else
	/* Single-core appliance: run forever; only a load failure returns
	 * (loud on the console), so retry.
	 */
	for (;;) {
		int rc = retro_frontend_run();

		printf("[retro] frontend exited: %d -- retrying\n", rc);
		k_msleep(2000);
	}
#endif
	return 0;
}
