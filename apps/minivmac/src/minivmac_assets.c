/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- embedded asset store: ROM (read-only) + boot disk
 * (writable).
 *
 * The ROM and a seed floppy are linked into .rodata (minivmac_pack.S,
 * .incbin); this file serves them through a POSIX fd layer, so OSGLUSDL.c's
 * fopen/fread/fwrite/fseek (funnelled by picolibc tinystdio through
 * open/read/write/lseek/close/fstat) resolve here with no filesystem on the
 * boot path and the Mini vMac tree untouched.
 *
 *  - ROM ("vMac.ROM"): read-only rodata. LoadMacRom() lands here.
 *  - Boot disk ("boot.dsk"): two backings, chosen at open time.
 *      * CONFIG_MINIVMAC_DISK_FS: a real file on a mounted FATFS volume (the
 *        SD card), via minivmac_storage.c -- writes PERSIST across reboot.
 *      * otherwise / on mount failure: a heap RAM overlay copied from the
 *        embedded floppy -- writable but non-persistent.
 */

#include <zephyr/kernel.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef CONFIG_MINIVMAC_DISK_FS
#include "minivmac_storage.h"
#endif

extern const uint8_t minivmac_rom_start[];
extern const uint8_t minivmac_rom_end[];
extern const uint8_t minivmac_disk_start[];
extern const uint8_t minivmac_disk_end[];

#define MINIVMAC_FD_BASE 3
#define MINIVMAC_FD_MAX  4

enum fd_kind {
	FD_FREE = 0,
	FD_ROM,
	FD_DISK_RAM,
	FD_DISK_SD,
};

static struct {
	size_t pos;
	enum fd_kind kind;
} fds[MINIVMAC_FD_MAX];

/* RAM overlay for the boot disk, materialized on first open (fallback path). */
static uint8_t *disk_ram;
static size_t disk_len;

static size_t rom_size(void)
{
	return (size_t)(minivmac_rom_end - minivmac_rom_start);
}

static size_t disk_image_size(void)
{
	return (size_t)(minivmac_disk_end - minivmac_disk_start);
}

static bool ends_with(const char *path, const char *suffix)
{
	if (path == NULL) {
		return false;
	}

	size_t n = strlen(path);
	size_t m = strlen(suffix);

	return n >= m && strcasecmp(path + n - m, suffix) == 0;
}

static bool is_rom(const char *path)
{
	return ends_with(path, "vMac.ROM");
}

static bool is_disk(const char *path)
{
	return ends_with(path, "boot.dsk");
}

static bool disk_ram_ready(void)
{
	if (disk_ram == NULL) {
		disk_len = disk_image_size();
		disk_ram = malloc(disk_len);
		if (disk_ram == NULL) {
			return false;
		}
		memcpy(disk_ram, minivmac_disk_start, disk_len);
	}
	return true;
}

int open(const char *path, int flags, ...)
{
	enum fd_kind kind;

	if (is_disk(path)) {
#ifdef CONFIG_MINIVMAC_DISK_FS
		if (minivmac_storage_disk_init() == 0) {
			kind = FD_DISK_SD;
		} else
#endif
		if (disk_image_size() == 0 || !disk_ram_ready()) {
			errno = ENOENT;
			return -1;
		} else {
			kind = FD_DISK_RAM;
		}
	} else if ((flags & O_ACCMODE) != O_RDONLY) {
		errno = EROFS;
		return -1;
	} else if (is_rom(path) && rom_size() > 0) {
		kind = FD_ROM;
	} else {
		errno = ENOENT;
		return -1;
	}

	for (int i = 0; i < MINIVMAC_FD_MAX; i++) {
		if (fds[i].kind == FD_FREE) {
			fds[i].pos = 0;
			fds[i].kind = kind;
			return MINIVMAC_FD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

static int fd_slot(int fd)
{
	int i = fd - MINIVMAC_FD_BASE;

	if (i < 0 || i >= MINIVMAC_FD_MAX || fds[i].kind == FD_FREE) {
		return -1;
	}
	return i;
}

static size_t slot_size(int i)
{
	switch (fds[i].kind) {
#ifdef CONFIG_MINIVMAC_DISK_FS
	case FD_DISK_SD:
		return minivmac_storage_disk_size();
#endif
	case FD_DISK_RAM:
		return disk_len;
	default:
		return rom_size();
	}
}

int close(int fd)
{
	int i = fd_slot(fd);

	if (i < 0) {
		errno = EBADF;
		return -1;
	}
	fds[i].kind = FD_FREE;
	return 0;
}

ssize_t read(int fd, void *buf, size_t count)
{
	int i = fd_slot(fd);

	if (i < 0) {
		errno = EBADF;
		return -1;
	}

#ifdef CONFIG_MINIVMAC_DISK_FS
	if (fds[i].kind == FD_DISK_SD) {
		ssize_t n = minivmac_storage_disk_read(buf, count, fds[i].pos);

		if (n < 0) {
			errno = EIO;
			return -1;
		}
		fds[i].pos += (size_t)n;
		return n;
	}
#endif

	const uint8_t *src = (fds[i].kind == FD_DISK_RAM) ? disk_ram : minivmac_rom_start;
	size_t avail = slot_size(i) - fds[i].pos;
	size_t n = MIN(count, avail);

	memcpy(buf, src + fds[i].pos, n);
	fds[i].pos += n;
	return (ssize_t)n;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	int i = fd_slot(fd);

	if (i < 0) {
		errno = EBADF;
		return -1;
	}

#ifdef CONFIG_MINIVMAC_DISK_FS
	if (fds[i].kind == FD_DISK_SD) {
		ssize_t n = minivmac_storage_disk_write(buf, count, fds[i].pos);

		if (n < 0) {
			errno = EIO;
			return -1;
		}
		fds[i].pos += (size_t)n;
		return n;
	}
#endif

	if (fds[i].kind != FD_DISK_RAM) {
		errno = EROFS;
		return -1;
	}

	/* Disk images are fixed size; writes stay within existing sectors. */
	size_t space = disk_len - fds[i].pos;
	size_t n = MIN(count, space);

	memcpy(disk_ram + fds[i].pos, buf, n);
	fds[i].pos += n;
	if (n < count) {
		errno = ENOSPC;
	}
	return (ssize_t)n;
}

off_t lseek(int fd, off_t offset, int whence)
{
	int i = fd_slot(fd);

	if (i < 0) {
		errno = EBADF;
		return (off_t)-1;
	}

	off_t fsize = (off_t)slot_size(i);
	off_t base;

	switch (whence) {
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = (off_t)fds[i].pos;
		break;
	case SEEK_END:
		base = fsize;
		break;
	default:
		errno = EINVAL;
		return (off_t)-1;
	}

	off_t target = base + offset;

	if (target < 0 || target > fsize) {
		errno = EINVAL;
		return (off_t)-1;
	}
	fds[i].pos = (size_t)target;
	return target;
}

int fstat(int fd, struct stat *st)
{
	int i = fd_slot(fd);

	if (i < 0) {
		errno = EBADF;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = (off_t)slot_size(i);
	st->st_mode = S_IFREG | ((fds[i].kind == FD_ROM) ? 0444 : 0644);
	return 0;
}

int stat(const char *path, struct stat *st)
{
	off_t size;
	mode_t mode;

	if (is_disk(path) && disk_image_size() > 0) {
		size = (off_t)disk_image_size();
		mode = S_IFREG | 0644;
	} else if (is_rom(path) && rom_size() > 0) {
		size = (off_t)rom_size();
		mode = S_IFREG | 0444;
	} else {
		errno = ENOENT;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = size;
	st->st_mode = mode;
	return 0;
}
