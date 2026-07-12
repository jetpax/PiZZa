/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * qemu ram-disk core seed (CONFIG_RETRO_QEMU_RAMDISK_SEED).
 */

#ifndef PIZZA_RETRO_QEMU_SEED_H
#define PIZZA_RETRO_QEMU_SEED_H

/* Format-mount the ram-disk and write the embedded cores into
 * CONFIG_RETRO_MENU_DIR so the launcher's fs scan sees them. */
void retro_qemu_seed(void);

#endif /* PIZZA_RETRO_QEMU_SEED_H */
