/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PIZZA_LIB_BOOTSEL_H_
#define PIZZA_LIB_BOOTSEL_H_

#include <stdbool.h>
#include <stddef.h>

#define BOOTSEL_MAX_ENTRIES 9	/* one digit key each in PiZZaBoot */
#define BOOTSEL_NAME_LEN    32
#define BOOTSEL_FILE_LEN    40

struct bootsel_entry {
	char name[BOOTSEL_NAME_LEN];	/* display label (menu.txt key) */
	char file[BOOTSEL_FILE_LEN];	/* kernel file on the boot FAT */
	bool present;			/* file exists on the card */
};

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

/*
 * Parse menu.txt at the boot FAT root ("Name = file.bin" lines;
 * "timeout" and "default" keys are reserved) and fs_stat each entry's
 * kernel file into .present. timeout_s / def_name are written only
 * when the corresponding key is present (pass NULL to skip). Returns
 * the entry count, 0 if menu.txt is absent or empty. Not thread-safe
 * (static parse buffer).
 */
int bootsel_load_menu(struct bootsel_entry *ents, int max,
		      int *timeout_s, char *def_name, size_t def_sz);

/* First line of chosen.txt into buf. Returns 0 or a negative error. */
int bootsel_get_chosen(char *buf, size_t sz);

/* chosen.txt exists, is well-formed, and names a file on the card. */
bool bootsel_chosen_valid(void);

/*
 * Reboot into the boot menu, and make it WAIT for input: persist
 * bootmenu.bin as the choice (a deleted choice is the fresh-card
 * state, whose countdown would auto-boot the default -- straight back
 * into the app that called this). Cards without bootmenu.bin fall
 * back to deleting chosen.txt. Returns only on error.
 */
int bootsel_menu(void);

/*
 * Boot by menu.txt entry name (case-insensitive) or directly by
 * kernel filename: persist via bootsel_set_kernel and reset. Returns
 * only on error.
 */
int bootsel_boot(const char *name_or_file);

#endif /* PIZZA_LIB_BOOTSEL_H_ */
