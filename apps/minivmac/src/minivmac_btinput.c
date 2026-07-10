/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- Bluetooth HID input (CONFIG_BTINPUT).
 *
 * Everything lives in apps/lib/btinput (manager = bring-up + connection
 * policy on its own thread; SDL seam = events into the sdl2shim queue that
 * OSGLUSDL.c already drains). Consumer policy here is minimal:
 *
 *  - NO keymap: a BT keyboard's usages pass through 1:1 (they ARE SDL
 *    scancodes), so the 8BitDo's stock K-mode letters TYPE letters into the
 *    Mac -- which is exactly the keyboard test. Mini vMac's SDLScan2MacKeyCode
 *    handles the rest.
 *  - Bounds = the Mac Plus screen (512x342): Mini vMac polls the absolute
 *    position via SDL_GetMouseState every tick, so the seam's integrated
 *    position maps 1:1 onto the Mac cursor.
 *  - Mount ordering: the manager mounts /SD: for the settings bond store,
 *    but this app backs its BOOT DISK with /SD:/minivmac.dsk and its own
 *    fs_mount falls back to a non-persistent RAM overlay on ANY error --
 *    including the -EBUSY it would get if the manager's thread won the
 *    mount race. Init the app's disk first (idempotent); the manager then
 *    takes the already-mounted path.
 */

#include <zephyr/kernel.h>

#include "btinput.h"
#include "minivmac_storage.h"

void minivmac_btinput_start(void)
{
#ifdef CONFIG_MINIVMAC_DISK_FS
	minivmac_storage_disk_init();
#endif

	btinput_seam_sdl_set_bounds(512, 342);
	btinput_seam_sdl_attach();
	btinput_manager_start();
}
