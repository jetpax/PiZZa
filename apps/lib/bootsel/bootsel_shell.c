/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * `boot` shell command -- the Tier-1 boot selection frontend.
 *
 * Surface: `boot list` (menu.txt entries + the current choice),
 * `boot menu` (drop the choice, reboot into PiZZaBoot), `boot <name>`
 * (persist an entry by name -- quote names with spaces -- or a kernel
 * filename, then reboot), `boot reboot` (plain PM reset honouring the
 * persisted choice), `boot gpio17` (force-menu button diagnostic).
 *
 * Must be pulled in via target_sources(app ...), never a static lib:
 * SHELL_CMD_REGISTER lives in an iterable linker section that archive
 * symbol resolution would drop.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <stdio.h>
#include <string.h>

#include "bootsel.h"

#if defined(CONFIG_SOC_BCM2835)
#define GPIO_BASE_PHYS 0x20200000UL
#else
#define GPIO_BASE_PHYS 0x3f200000UL
#endif

#define GPLEV0 0x34

static int cmd_boot_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "PM_RSTS partition 0, full watchdog reset...");
	k_sleep(K_MSEC(50));
	bootsel_reboot();
	return 0;
}

static int cmd_boot_gpio17(const struct shell *sh, size_t argc, char **argv)
{
	static mm_reg_t gpio;
	uint32_t lev;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (gpio == 0) {
		device_map(&gpio, GPIO_BASE_PHYS, 0x100, K_MEM_CACHE_NONE);
	}
	lev = sys_read32(gpio + GPLEV0);

	shell_print(sh, "GPIO17 level = %u", (unsigned int)((lev >> 17) & 1U));
	return 0;
}

#if defined(CONFIG_FILE_SYSTEM)
static int cmd_boot_list(const struct shell *sh, size_t argc, char **argv)
{
	struct bootsel_entry ents[BOOTSEL_MAX_ENTRIES];
	char chosen[64] = "";
	char def[BOOTSEL_NAME_LEN] = "";
	int timeout_s = -1;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int n = bootsel_load_menu(ents, BOOTSEL_MAX_ENTRIES, &timeout_s,
				  def, sizeof(def));

	if (n == 0) {
		shell_print(sh, "no menu.txt on the boot FAT");
		return 0;
	}
	(void)bootsel_get_chosen(chosen, sizeof(chosen));

	for (int i = 0; i < n; i++) {
		char line[16];

		snprintf(line, sizeof(line), "kernel=%s", ents[i].file);
		shell_print(sh, "%c %-24s %-20s%s",
			    (chosen[0] != '\0' &&
			     strncmp(chosen, line, strlen(line)) == 0) ?
				    '*' : ' ',
			    ents[i].name, ents[i].file,
			    ents[i].present ? "" : " (missing)");
	}
	if (chosen[0] == '\0') {
		shell_print(sh, "(no choice persisted: menu boots)");
	}
	return 0;
}

static int cmd_boot_menu(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "dropping choice, rebooting into the boot menu...");
	k_sleep(K_MSEC(50));

	int rc = bootsel_menu();

	shell_error(sh, "bootsel_menu failed (%d)", rc);
	return rc;
}

static int cmd_boot_root(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_help(sh);
		return 1;
	}
	shell_print(sh, "booting %s ...", argv[1]);
	k_sleep(K_MSEC(50));

	int rc = bootsel_boot(argv[1]);

	shell_error(sh, "boot %s failed (%d)", argv[1], rc);
	return rc;
}
#else
#define cmd_boot_root NULL
#endif /* CONFIG_FILE_SYSTEM */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_boot,
#if defined(CONFIG_FILE_SYSTEM)
	SHELL_CMD(list, NULL,
		  "List menu.txt entries; * marks the persisted choice",
		  cmd_boot_list),
	SHELL_CMD(menu, NULL,
		  "Drop the persisted choice and reboot into the boot menu",
		  cmd_boot_menu),
#endif
	SHELL_CMD(reboot, NULL,
		  "PM watchdog full reset, partition 0 (normal boot)",
		  cmd_boot_reboot),
	SHELL_CMD(gpio17, NULL,
		  "Read the GPIO17 (force-menu button) input level",
		  cmd_boot_gpio17),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(boot, &sub_boot,
		   "Tier-1 boot selection (boot <name> | list | menu | reboot)",
		   cmd_boot_root);
