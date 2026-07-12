/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- shared storage mount.
 */

#ifndef PIZZA_RETRO_STORAGE_H
#define PIZZA_RETRO_STORAGE_H

/* Idempotent mount of CONFIG_RETRO_STORAGE_MOUNT. Tolerates a volume
 * the btinput manager (or a prior call) already mounted. Returns 0 if
 * the volume is usable, negative otherwise.
 */
int retro_storage_mount(void);

#endif /* PIZZA_RETRO_STORAGE_H */
