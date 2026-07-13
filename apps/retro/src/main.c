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
	printf("[retro] RetroPiZZa launcher, board %s\n", CONFIG_BOARD);

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
	/* Launcher: pick a core; open it (bind + init) so its content needs
	 * are known; browse content if it needs any; run until the user asks
	 * for the menu (Home / L+R / `retro menu`); tear down; repeat. A
	 * bind/load failure or a content-selection back-out returns to the
	 * picker.
	 */
	for (;;) {
		static char core_path[128];
		static char content_path[128];

		if (retro_menu_run(core_path, sizeof(core_path)) != 0) {
			k_msleep(1000);
			continue;
		}
		retro_core_set_path(core_path);

		if (retro_frontend_open() != 0) {
			continue;
		}

		const struct retro_content_req *req =
			retro_frontend_content_req();

		content_path[0] = '\0';
		if (req->wants_content) {
			if (retro_menu_browse_content(req->library_name,
						      req->valid_extensions,
						      content_path,
						      sizeof(content_path)) != 0) {
				retro_frontend_close();
				continue;
			}
		}

		retro_frontend_run_loaded(content_path[0] ? content_path : NULL);
		retro_frontend_close();
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
