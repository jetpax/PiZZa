/* Compat sys/statvfs.h — drive_local.cpp queries host free space for
 * the DOS drive-info call; report "unavailable" and DOSBox falls back
 * to its default fake geometry.
 */
#ifndef PIZZA_COMPAT_STATVFS_H
#define PIZZA_COMPAT_STATVFS_H

#include <stdint.h>

struct statvfs {
	unsigned long f_bsize;
	unsigned long f_frsize;
	uint64_t f_blocks;
	uint64_t f_bfree;
	uint64_t f_bavail;
	uint64_t f_files;
	uint64_t f_ffree;
	uint64_t f_favail;
	unsigned long f_fsid;
	unsigned long f_flag;
	unsigned long f_namemax;
};

static inline int statvfs(const char *path, struct statvfs *buf)
{
	(void)path; (void)buf;
	return -1;
}

#endif
