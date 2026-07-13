/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- shared storage mount (CONFIG_FILE_SYSTEM).
 *
 * One FATFS mount for the whole frontend: the menu scans the cores dir
 * and the llext loader reads the chosen core, both off the same
 * volume. On HW the btinput manager may have mounted it already
 * (-EBUSY / statvfs-visible); on qemu this owns the (ram-disk) mount.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <ff.h>

#include "retro_storage.h"

LOG_MODULE_REGISTER(retro_storage, CONFIG_RETRO_LOG_LEVEL);

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = CONFIG_RETRO_STORAGE_MOUNT,
};

/* The standard content layout the frontend + cores expect on the volume.
 * roms = content browsed by the launcher; system/saves = the directory
 * env commands; states = save-state slots. Created idempotently on mount
 * (-EEXIST on a card that already has them is fine).
 */
static const char *const std_dirs[] = {
	CONFIG_RETRO_STORAGE_MOUNT "/roms",
	CONFIG_RETRO_STORAGE_MOUNT "/system",
	CONFIG_RETRO_STORAGE_MOUNT "/saves",
	CONFIG_RETRO_STORAGE_MOUNT "/states",
};

static void make_std_dirs(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(std_dirs); i++) {
		int rc = fs_mkdir(std_dirs[i]);

		if (rc != 0 && rc != -EEXIST) {
			LOG_WRN("mkdir %s failed (%d)", std_dirs[i], rc);
		}
	}
}

int retro_storage_mount(void)
{
	static bool mounted;
	struct fs_statvfs st;
	int rc;

	if (mounted) {
		return 0;
	}

	/* Someone else (btinput manager) may own it -- probe first so a
	 * shared volume doesn't log a loud fs_mount error.
	 */
	if (fs_statvfs(CONFIG_RETRO_STORAGE_MOUNT, &st) == 0) {
		mounted = true;
		make_std_dirs();
		return 0;
	}

	rc = fs_mount(&mp);
	if (rc == 0 || rc == -EBUSY) {
		mounted = true;
		make_std_dirs();
		return 0;
	}

	LOG_ERR("mount %s failed (%d)", CONFIG_RETRO_STORAGE_MOUNT, rc);
	return rc;
}
