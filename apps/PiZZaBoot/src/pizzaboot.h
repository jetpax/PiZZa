/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PIZZABOOT_H_
#define PIZZABOOT_H_

#include <stdbool.h>

#define PB_MAX_ENTRIES 9	/* one digit key each */
#define PB_NAME_LEN    32
#define PB_FILE_LEN    40

struct pb_entry {
	char name[PB_NAME_LEN];	/* display label (menu.txt key) */
	char file[PB_FILE_LEN];	/* kernel file on the boot FAT */
	bool present;		/* file exists on the card */
};

/* HDMI list renderer (hdmi.c). status may be NULL. */
int hdmi_init(void);
void hdmi_render(const struct pb_entry *ents, int n, int sel,
		 const char *status);

#endif /* PIZZABOOT_H_ */
