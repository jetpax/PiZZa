/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * `boot` shell command -- the Tier-1 boot selection frontend.
 *
 * Current surface: `boot reboot` (PM watchdog full reset, normal boot)
 * and `boot gpio17` (input-level diagnostic for the config.txt
 * [gpio17=...] force-menu conditional). Grows `boot list | menu |
 * <name>` in WO-T1 M2.
 *
 * Must be pulled in via target_sources(app ...), never a static lib:
 * SHELL_CMD_REGISTER lives in an iterable linker section that archive
 * symbol resolution would drop.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>

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

SHELL_STATIC_SUBCMD_SET_CREATE(sub_boot,
	SHELL_CMD(reboot, NULL,
		  "PM watchdog full reset, partition 0 (normal boot)",
		  cmd_boot_reboot),
	SHELL_CMD(gpio17, NULL,
		  "Read the GPIO17 (force-menu button) input level",
		  cmd_boot_gpio17),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(boot, &sub_boot, "Tier-1 boot selection", NULL);
