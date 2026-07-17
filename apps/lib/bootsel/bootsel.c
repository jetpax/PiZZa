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
#include <stdlib.h>
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

#define BOOTSEL_MOUNT       "/SD:"
#define CHOSEN_TXT          BOOTSEL_MOUNT "/chosen.txt"
#define CHOSEN_NEW          BOOTSEL_MOUNT "/chosen.new"
#define BOOTSEL_MENU_KERNEL "bootmenu.bin"
#define LINE_MAX_LEN        64

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

#define MENU_TXT  BOOTSEL_MOUNT "/menu.txt"

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	char *e = s + strlen(s);

	while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
			 e[-1] == '\r' || e[-1] == '\n')) {
		*--e = '\0';
	}
	return s;
}

static bool str_ieq(const char *a, const char *b)
{
	for (; *a != '\0' && *b != '\0'; a++, b++) {
		if ((*a | 0x20) != (*b | 0x20)) {
			return false;
		}
	}
	return *a == *b;
}

int bootsel_load_menu(struct bootsel_entry *ents, int max,
		      int *timeout_s, char *def_name, size_t def_sz,
		      char *shell_file, size_t shell_sz)
{
	static char buf[2048];
	struct fs_file_t f;
	ssize_t n;

	fs_file_t_init(&f);
	if (fs_open(&f, MENU_TXT, FS_O_READ) != 0) {
		return 0;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	(void)fs_close(&f);
	if (n <= 0) {
		return 0;
	}
	buf[n] = '\0';

	int count = 0;
	char *save = NULL;

	for (char *line = strtok_r(buf, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *s = trim(line);

		if (s[0] == '\0' || s[0] == '#') {
			continue;
		}
		char *eq = strchr(s, '=');

		if (eq == NULL) {
			continue;
		}
		*eq = '\0';
		char *key = trim(s);
		char *val = trim(eq + 1);

		if (str_ieq(key, "timeout")) {
			if (timeout_s != NULL) {
				*timeout_s = atoi(val);
			}
			continue;
		}
		if (str_ieq(key, "default")) {
			if (def_name != NULL && def_sz > 0) {
				strncpy(def_name, val, def_sz - 1);
				def_name[def_sz - 1] = '\0';
			}
			continue;
		}
		if (str_ieq(key, "shell")) {
			if (shell_file != NULL && shell_sz > 0) {
				strncpy(shell_file, val, shell_sz - 1);
				shell_file[shell_sz - 1] = '\0';
			}
			continue;
		}
		if (count < max && key[0] != '\0' && val[0] != '\0') {
			memset(&ents[count], 0, sizeof(ents[count]));
			strncpy(ents[count].name, key, BOOTSEL_NAME_LEN - 1);
			strncpy(ents[count].file, val, BOOTSEL_FILE_LEN - 1);
			count++;
		}
	}

	char path[LINE_MAX_LEN];
	struct fs_dirent st;

	for (int i = 0; i < count; i++) {
		snprintf(path, sizeof(path), BOOTSEL_MOUNT "/%s",
			 ents[i].file);
		ents[i].present = (fs_stat(path, &st) == 0 &&
				   st.type == FS_DIR_ENTRY_FILE);
	}
	return count;
}

int bootsel_get_chosen(char *buf, size_t sz)
{
	struct fs_file_t f;
	ssize_t n;

	fs_file_t_init(&f);
	int rc = fs_open(&f, CHOSEN_TXT, FS_O_READ);

	if (rc != 0) {
		return rc;
	}
	n = fs_read(&f, buf, sz - 1);
	(void)fs_close(&f);
	if (n <= 0) {
		return -EIO;
	}
	buf[n] = '\0';

	char *nl = strchr(buf, '\n');

	if (nl != NULL) {
		*nl = '\0';
	}
	return 0;
}

bool bootsel_chosen_valid(void)
{
	char buf[LINE_MAX_LEN];
	char path[LINE_MAX_LEN];
	struct fs_dirent st;

	if (bootsel_get_chosen(buf, sizeof(buf)) != 0) {
		return false;
	}
	if (strncmp(buf, "kernel=", 7) != 0) {
		return false;
	}
	snprintf(path, sizeof(path), BOOTSEL_MOUNT "/%s", trim(buf + 7));
	return fs_stat(path, &st) == 0 && st.type == FS_DIR_ENTRY_FILE;
}

int bootsel_menu(void)
{
	/* Persist the MENU as the choice rather than deleting it: a
	 * deleted choice is the fresh-card state, and the menu then
	 * counts down into the default entry -- which round-trips the
	 * user straight back into the app they just exited. A valid
	 * choice naming the menu kernel makes PiZZaBoot wait for input.
	 */
	int rc = bootsel_set_kernel(BOOTSEL_MENU_KERNEL);

	if (rc == -ENOENT) {
		/* No bootmenu.bin (single-app card): dropping the choice
		 * boots the config.txt default, the menu on any WO-T1
		 * card.
		 */
		rc = fs_unlink(CHOSEN_TXT);
		if (rc == -ENOENT) {
			rc = 0;
		}
	}
	if (rc != 0) {
		return rc;
	}
	bootsel_reboot();
	return 0;
}

int bootsel_boot(const char *name_or_file)
{
	struct bootsel_entry ents[BOOTSEL_MAX_ENTRIES];
	int n = bootsel_load_menu(ents, BOOTSEL_MAX_ENTRIES, NULL, NULL, 0,
				  NULL, 0);
	const char *file = name_or_file;

	for (int i = 0; i < n; i++) {
		if (str_ieq(ents[i].name, name_or_file)) {
			file = ents[i].file;
			break;
		}
	}

	int rc = bootsel_set_kernel(file);

	if (rc != 0) {
		return rc;
	}
	bootsel_reboot();
	return 0;
}

#endif /* CONFIG_FILE_SYSTEM */
