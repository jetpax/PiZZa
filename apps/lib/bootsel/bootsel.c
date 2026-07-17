/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * bootsel -- Tier-1 boot selection helpers shared by every PiZZa app.
 *
 * The reset sequence is Linux's __bcm2835_restart()
 * (drivers/watchdog/bcm2835_wdt.c) ported to Zephyr: encode the target
 * MBR partition into PM_RSTS bits 0,2,4,6,8,10 (0 = normal boot, so
 * the firmware re-reads config.txt), arm the PM watchdog with a
 * ~150 us timeout, then request a full reset. Every PM write must
 * carry the 0x5a password in the top byte.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <stdio.h>
#include <string.h>

#if defined(CONFIG_FILE_SYSTEM)
#include <zephyr/fs/fs.h>
#endif

#include "bootsel.h"

#if defined(CONFIG_SOC_BCM2835)
#define PM_BASE_PHYS 0x20100000UL
#else
#define PM_BASE_PHYS 0x3f100000UL
#endif

#define PM_RSTC 0x1c
#define PM_RSTS 0x20
#define PM_WDOG 0x24

#define PM_PASSWORD           0x5a000000U
#define PM_RSTS_PARTITION_CLR 0xfffffaaaU
#define PM_RSTC_WRCFG_CLR     0xffffffcfU
#define PM_RSTC_FULL_RESET    0x00000020U

void bootsel_reboot(void)
{
	mm_reg_t pm;
	uint32_t v;

	(void)irq_lock();

	device_map(&pm, PM_BASE_PHYS, 0x28, K_MEM_CACHE_NONE);

	v = sys_read32(pm + PM_RSTS) & PM_RSTS_PARTITION_CLR;
	sys_write32(PM_PASSWORD | v, pm + PM_RSTS);

	sys_write32(PM_PASSWORD | 10, pm + PM_WDOG);
	v = sys_read32(pm + PM_RSTC) & PM_RSTC_WRCFG_CLR;
	sys_write32(PM_PASSWORD | v | PM_RSTC_FULL_RESET, pm + PM_RSTC);

	for (;;) {
		/* watchdog fires within ~150 us */
	}
}

#if defined(CONFIG_FILE_SYSTEM)

#define BOOTSEL_MOUNT  "/SD:"
#define CHOSEN_TXT     BOOTSEL_MOUNT "/chosen.txt"
#define CHOSEN_NEW     BOOTSEL_MOUNT "/chosen.new"
#define LINE_MAX_LEN   64

int bootsel_set_kernel(const char *file)
{
	char path[LINE_MAX_LEN];
	char line[LINE_MAX_LEN];
	char back[LINE_MAX_LEN];
	struct fs_dirent st;
	struct fs_file_t f;
	ssize_t n;
	int len;
	int rc;

	len = snprintf(path, sizeof(path), BOOTSEL_MOUNT "/%s", file);
	if (len < 0 || len >= (int)sizeof(path)) {
		return -ENAMETOOLONG;
	}
	rc = fs_stat(path, &st);
	if (rc != 0) {
		return rc;
	}
	if (st.type != FS_DIR_ENTRY_FILE) {
		return -ENOENT;
	}

	len = snprintf(line, sizeof(line), "kernel=%s\n", file);
	if (len < 0 || len >= (int)sizeof(line)) {
		return -ENAMETOOLONG;
	}

	fs_file_t_init(&f);
	rc = fs_open(&f, CHOSEN_NEW, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc != 0) {
		return rc;
	}
	n = fs_write(&f, line, len);
	rc = fs_close(&f);
	if (n != len || rc != 0) {
		return (rc != 0) ? rc : -EIO;
	}

	fs_file_t_init(&f);
	rc = fs_open(&f, CHOSEN_NEW, FS_O_READ);
	if (rc != 0) {
		return rc;
	}
	n = fs_read(&f, back, sizeof(back));
	(void)fs_close(&f);
	if (n != len || memcmp(back, line, len) != 0) {
		return -EIO;
	}

	/* The unlink->rename window is benign: no chosen.txt means the
	 * firmware default (the menu) boots.
	 */
	rc = fs_unlink(CHOSEN_TXT);
	if (rc != 0 && rc != -ENOENT) {
		return rc;
	}
	return fs_rename(CHOSEN_NEW, CHOSEN_TXT);
}

#endif /* CONFIG_FILE_SYSTEM */
