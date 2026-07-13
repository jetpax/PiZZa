/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * qemu ram-disk core seed (CONFIG_RETRO_QEMU_RAMDISK_SEED).
 *
 * qemu has no SD, so the launcher's fs scan would find nothing. This
 * writes two cores baked into the image (retro_qemu_seed_pack.S) onto
 * the formatted ram-disk under the menu dir, reproducing the exact HW
 * path: fs scan -> fs-loader load -> in-process swap.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>

#include "retro_qemu_seed.h"
#include "retro_storage.h"

LOG_MODULE_REGISTER(retro_seed, CONFIG_RETRO_LOG_LEVEL);

extern const uint8_t seed_core_a_start[], seed_core_a_end[];
extern const uint8_t seed_core_b_start[], seed_core_b_end[];
#ifdef RETRO_HAVE_SEED_WAD
extern const uint8_t seed_wad_start[], seed_wad_end[];
#endif

/* The on-disk names the cores are seeded under (the menu lists them by
 * filename). CMake passes the actual llext basenames; the defaults keep
 * the M4 two-core scenario working unchanged.
 */
#ifndef SEED_CORE_A_NAME
#define SEED_CORE_A_NAME "2048.llext"
#endif
#ifndef SEED_CORE_B_NAME
#define SEED_CORE_B_NAME "sokoban.llext"
#endif

static void write_blob(const char *dir, const char *name,
		       const uint8_t *start, const uint8_t *end)
{
	char path[128];
	struct fs_file_t f;
	size_t len = end - start;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	fs_file_t_init(&f);

	if (fs_open(&f, path, FS_O_CREATE | FS_O_WRITE) != 0) {
		LOG_ERR("seed: open %s failed", path);
		return;
	}
	ssize_t w = fs_write(&f, start, len);

	fs_close(&f);
	LOG_INF("seed: %s (%zd/%zu bytes)", path, w, len);
}

void retro_qemu_seed(void)
{
	if (retro_storage_mount() != 0) {
		LOG_ERR("seed: ram-disk mount failed");
		return;
	}
	fs_mkdir(CONFIG_RETRO_MENU_DIR); /* -EEXIST is fine */

	write_blob(CONFIG_RETRO_MENU_DIR, SEED_CORE_A_NAME,
		   seed_core_a_start, seed_core_a_end);
	write_blob(CONFIG_RETRO_MENU_DIR, SEED_CORE_B_NAME,
		   seed_core_b_start, seed_core_b_end);

#ifdef RETRO_HAVE_SEED_WAD
	/* Content core (fstest/Doom) gate: drop a WAD into the browsed
	 * content dir so the launcher's browse -> pick -> load path runs
	 * in sim, exactly as it will off /SD:/roms on hardware.
	 */
	fs_mkdir(CONFIG_RETRO_CONTENT_DIR); /* -EEXIST is fine */
	write_blob(CONFIG_RETRO_CONTENT_DIR, RETRO_SEED_WAD_NAME,
		   seed_wad_start, seed_wad_end);
#endif
}
