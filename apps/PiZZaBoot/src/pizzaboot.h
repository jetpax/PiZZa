/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PIZZABOOT_H_
#define PIZZABOOT_H_

#include "bootsel.h"
#include "pizza_version.h"

#define PB_MAX_ENTRIES BOOTSEL_MAX_ENTRIES
#define PB_NAME_LEN    BOOTSEL_NAME_LEN
#define PB_FILE_LEN    BOOTSEL_FILE_LEN

/* HDMI list renderer (hdmi.c). status may be NULL. */
int hdmi_init(void);
void hdmi_render(const struct bootsel_entry *ents, int n, int sel,
		 const char *status);

#endif /* PIZZABOOT_H_ */
