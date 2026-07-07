/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- embedded asset store: ROM (read-only) + boot disk
 * (writable RAM overlay).
 *
 * Both are linked into .rodata (minivmac_pack.S, .incbin); this file serves
 * them through a POSIX fd layer, so OSGLUSDL.c's fopen/fread/fwrite/fseek
 * (funnelled by picolibc tinystdio through open/read/write/lseek/close/
 * fstat) resolve here with no filesystem on the boot path and the Mini vMac
 * tree untouched.
 *
 *  - ROM ("vMac.ROM"): read-only rodata. LoadMacRom() lands here.
 *  - Boot disk ("boot.dsk"): Sony_Insert1 opens it "rb+"; the guest OS writes
 *    to disk (desktop DB, prefs), so on first open the embedded image is
 *    copied into a heap buffer that reads/writes then hit. Non-persistent
 *    (writes lost on reboot); SD/FAT persistence is a later phase.
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

extern const uint8_t minivmac_rom_start[];
extern const uint8_t minivmac_rom_end[];
extern const uint8_t minivmac_disk_start[];
extern const uint8_t minivmac_disk_end[];

#define MINIVMAC_FD_BASE 3
#define MINIVMAC_FD_MAX  4

enum fd_kind {
	FD_FREE = 0,
	FD_ROM,
	FD_DISK,
};

static struct {
	size_t pos;
	enum fd_kind kind;
} fds[MINIVMAC_FD_MAX];

/* Writable RAM overlay for the boot disk, materialized on first open. */
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

/* Copy the embedded image into a heap buffer the first time the disk is
 * opened; later opens (re-insert) reuse it so writes persist for the session.
 */
static bool disk_ready(void)
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
		if (disk_image_size() == 0 || !disk_ready()) {
			errno = ENOENT;
			return -1;
		}
		kind = FD_DISK;   /* read or write both OK (RAM-backed) */
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
	return (fds[i].kind == FD_DISK) ? disk_len : rom_size();
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

	const uint8_t *src = (fds[i].kind == FD_DISK) ? disk_ram : minivmac_rom_start;
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
	if (fds[i].kind != FD_DISK) {
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
	st->st_mode = S_IFREG | ((fds[i].kind == FD_DISK) ? 0644 : 0444);
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
