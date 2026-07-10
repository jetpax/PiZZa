/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- persistent disk backing on a FATFS volume
 * (CONFIG_MINIVMAC_DISK_FS).
 *
 * The boot disk lives as a real file (minivmac.dsk) on a mounted FATFS
 * volume: the microSD card on rpi_zero_2w (the BCM SDHost driver +
 * zephyr,sdmmc-disk "SD"), or a RAM-backed FAT disk on qemu for the sim
 * gate. Guest writes therefore survive a reboot. On first run the file is
 * seeded from the embedded floppy (minivmac_pack.S). minivmac_assets.c routes
 * the disk fd here when the mount succeeds and falls back to the RAM overlay
 * when it does not, so nothing regresses if there is no card.
 *
 * The mount NEVER formats the SD card: CONFIG_FS_FATFS_MOUNT_MKFS (which
 * auto-formats a blank volume) is enabled only on the qemu RAM disk, so a
 * mount failure on hardware falls back to RAM rather than wiping the card.
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <ff.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "minivmac_storage.h"

#ifdef CONFIG_MINIVMAC_DISK_SELFTEST
static void disk_selftest(void);
#endif

LOG_MODULE_REGISTER(minivmac_storage, CONFIG_SDL2SHIM_LOG_LEVEL);

extern const uint8_t minivmac_disk_start[];
extern const uint8_t minivmac_disk_end[];

#define DISK_FILE  CONFIG_MINIVMAC_FS_MOUNT "/minivmac.dsk"

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = CONFIG_MINIVMAC_FS_MOUNT,
};

static struct fs_file_t disk_file;
static bool ready;
static size_t disk_len;

static size_t seed_size(void)
{
	return (size_t)(minivmac_disk_end - minivmac_disk_start);
}

/* First run on a fresh volume: materialize minivmac.dsk from the embedded
 * floppy. Existing files (a card the user already provisioned, or a prior
 * boot) are left untouched so their writes persist.
 */
static int seed_if_absent(void)
{
	struct fs_dirent ent;

	if (fs_stat(DISK_FILE, &ent) == 0) {
		return 0;
	}

	struct fs_file_t f;

	fs_file_t_init(&f);

	int rc = fs_open(&f, DISK_FILE, FS_O_CREATE | FS_O_WRITE);

	if (rc != 0) {
		LOG_ERR("seed: fs_open(%s) = %d", DISK_FILE, rc);
		return rc;
	}

	ssize_t w = fs_write(&f, minivmac_disk_start, seed_size());

	fs_close(&f);
	if (w < 0 || (size_t)w != seed_size()) {
		LOG_ERR("seed: fs_write = %d (want %zu)", (int)w, seed_size());
		return -EIO;
	}
	LOG_INF("seeded %s from embedded floppy (%zu bytes)", DISK_FILE, seed_size());
	return 0;
}

int minivmac_storage_disk_init(void)
{
	if (ready) {
		return 0;
	}

	int rc = fs_mount(&mp);

	if (rc != 0) {
		LOG_WRN("fs_mount(%s) = %d; using the RAM disk overlay instead",
			mp.mnt_point, rc);
		return rc;
	}

#ifdef CONFIG_MINIVMAC_DISK_SELFTEST
	/* Force a fresh seed each boot so the round-trip test exercises the WRITE
	 * path with the CURRENT driver -- the card may still hold data an older
	 * (buggy) driver wrote, which would otherwise read back "corrupt" no
	 * matter how good the read path is.
	 */
	fs_unlink(DISK_FILE);
#endif
	if (seed_if_absent() != 0) {
		return -EIO;
	}

	fs_file_t_init(&disk_file);
	rc = fs_open(&disk_file, DISK_FILE, FS_O_RDWR);
	if (rc != 0) {
		LOG_ERR("fs_open(%s, rw) = %d", DISK_FILE, rc);
		return rc;
	}

	struct fs_dirent ent;

	disk_len = (fs_stat(DISK_FILE, &ent) == 0) ? (size_t)ent.size : seed_size();
	ready = true;
	LOG_INF("disk-fs ready: %s (%zu bytes)", DISK_FILE, disk_len);
#ifdef CONFIG_MINIVMAC_DISK_SELFTEST
	disk_selftest();
#endif
	return 0;
}

size_t minivmac_storage_disk_size(void)
{
	return disk_len;
}

ssize_t minivmac_storage_disk_read(void *buf, size_t n, size_t pos)
{
	if (fs_seek(&disk_file, (off_t)pos, FS_SEEK_SET) != 0) {
		return -1;
	}
	return fs_read(&disk_file, buf, n);
}

ssize_t minivmac_storage_disk_write(const void *buf, size_t n, size_t pos)
{
	if (fs_seek(&disk_file, (off_t)pos, FS_SEEK_SET) != 0) {
		return -1;
	}
	return fs_write(&disk_file, buf, n);
}

#ifdef CONFIG_MINIVMAC_DISK_SELFTEST
/* Read the whole disk back off the volume and diff it against the embedded
 * seed. On the SD card this confirms whether the SDHost PIO data path
 * round-trips faithfully (the disk-? symptom vs the RAM overlay that boots).
 */
static void disk_selftest(void)
{
	static uint8_t buf[512] __aligned(64);
	size_t sectors = seed_size() / 512;
	size_t bad = 0;
	size_t reported = 0;

	LOG_INF("selftest: reading %zu sectors back from %s ...", sectors, DISK_FILE);

	for (size_t s = 0; s < sectors; s++) {
		size_t off = s * 512;
		ssize_t r = minivmac_storage_disk_read(buf, 512, off);

		if (r != 512) {
			LOG_ERR("selftest: read @sector %zu (off %zu) = %d, aborting",
				s, off, (int)r);
			return;
		}
		if (memcmp(buf, minivmac_disk_start + off, 512) != 0) {
			bad++;
			if (reported < 4) {
				for (int k = 0; k < 512; k++) {
					if (buf[k] != minivmac_disk_start[off + k]) {
						LOG_ERR("selftest: sector %zu byte %d differs: "
							"sd=0x%02x seed=0x%02x", s, k, buf[k],
							minivmac_disk_start[off + k]);
						break;
					}
				}
				reported++;
			}
		}
	}

	if (bad == 0) {
		LOG_INF("selftest: SD round-trip OK -- all %zu sectors match the seed",
			sectors);
	} else {
		LOG_ERR("selftest: SD DATA CORRUPT -- %zu / %zu sectors differ from the seed",
			bad, sectors);
	}
}
#endif
