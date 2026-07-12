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

static void write_core(const char *name, const uint8_t *start,
		       const uint8_t *end)
{
	char path[128];
	struct fs_file_t f;
	size_t len = end - start;

	snprintf(path, sizeof(path), "%s/%s", CONFIG_RETRO_MENU_DIR, name);
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

	write_core("2048.llext", seed_core_a_start, seed_core_a_end);
	write_core("sokoban.llext", seed_core_b_start, seed_core_b_end);
}
