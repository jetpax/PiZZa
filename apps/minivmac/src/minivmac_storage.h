/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MINIVMAC_STORAGE_H
#define MINIVMAC_STORAGE_H

#include <stddef.h>
#include <sys/types.h>

/* Persistent disk backing on a mounted FATFS volume (CONFIG_MINIVMAC_DISK_FS).
 * minivmac_assets.c calls these when routing the disk fd; on failure it falls
 * back to the RAM overlay. pos-explicit (pread/pwrite style) so the fd layer
 * keeps its own position like the ROM / RAM paths.
 */

int minivmac_storage_disk_init(void);   /* 0 = FS-backed disk mounted + open */
size_t minivmac_storage_disk_size(void);
ssize_t minivmac_storage_disk_read(void *buf, size_t n, size_t pos);
ssize_t minivmac_storage_disk_write(const void *buf, size_t n, size_t pos);

#endif /* MINIVMAC_STORAGE_H */
