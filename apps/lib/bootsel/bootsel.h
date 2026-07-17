/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PIZZA_LIB_BOOTSEL_H_
#define PIZZA_LIB_BOOTSEL_H_

/*
 * Reset the SoC via the PM watchdog with the PM_RSTS boot-partition
 * field cleared to 0, so the GPU firmware performs a normal boot and
 * re-reads config.txt. Does not return.
 */
void bootsel_reboot(void);

/*
 * Atomically persist the boot choice: write "kernel=<file>" to
 * chosen.new on the boot FAT (mounted at /SD:), verify the target file
 * exists and the line reads back exactly, then rename over chosen.txt.
 *
 * The write discipline is load-bearing (WO-T1 §5c): the GPU firmware
 * parses ANY kernel token in an included file as a live assignment --
 * including the undocumented whitespace form -- so a torn or mangled
 * chosen.txt bricks the boot until the force-menu button is held.
 *
 * Returns 0 on success, -ENOENT if <file> is not on the card, else a
 * negative fs error.
 */
int bootsel_set_kernel(const char *file);

#endif /* PIZZA_LIB_BOOTSEL_H_ */
