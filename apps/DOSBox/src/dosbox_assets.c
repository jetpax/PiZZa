/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa DOSBox-X -- baked-asset fd layer (Doom doom_assets.c pattern).
 *
 * The FAT floppy image is linked into .rodata (dosbox_pack.S) and
 * copied into a RAM buffer on first open, so IMGMOUNT's read-write
 * fopen works and DOS can write files onto A:. picolibc tinystdio
 * funnels fopen/fread/fseek/fwrite through open/read/lseek/write/
 * close/fstat, all of which resolve here. Everything that is not the
 * floppy image is ENOENT (config files, mapper files: absent by
 * design, their callers fall back to defaults).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern const uint8_t dbx_disk_start[];
extern const uint8_t dbx_disk_end[];

#define DBX_FD_BASE 3
#define DBX_FD_MAX  8

static struct {
	bool used;
	size_t pos;
} fds[DBX_FD_MAX];

static uint8_t *disk_ram;
static size_t disk_size;

static bool is_image(const char *path)
{
	if (path == NULL) {
		return false;
	}

	size_t n = strlen(path);

	return n >= 4 && strcasecmp(path + n - 4, ".img") == 0;
}

static bool disk_ready(void)
{
	if (disk_ram != NULL) {
		return true;
	}
	disk_size = (size_t)(dbx_disk_end - dbx_disk_start);
	if (disk_size == 0) {
		return false;
	}
	disk_ram = malloc(disk_size);
	if (disk_ram == NULL) {
		return false;
	}
	memcpy(disk_ram, dbx_disk_start, disk_size);
	printk("[dbx] disk image staged to RAM (%u KiB)\n",
	       (unsigned)(disk_size / 1024));
	return true;
}

int open(const char *path, int flags, ...)
{
	(void)flags;
	if (!is_image(path) || !disk_ready()) {
		errno = ENOENT;
		return -1;
	}

	for (int i = 0; i < DBX_FD_MAX; i++) {
		if (!fds[i].used) {
			fds[i].used = true;
			fds[i].pos = 0;
			return DBX_FD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

static int fd_slot(int fd)
{
	int i = fd - DBX_FD_BASE;

	if (i < 0 || i >= DBX_FD_MAX || !fds[i].used) {
		return -1;
	}
	return i;
}

int close(int fd)
{
	int i = fd_slot(fd);

	if (i < 0) {
		errno = EBADF;
		return -1;
	}
	fds[i].used = false;
	return 0;
}

ssize_t read(int fd, void *buf, size_t count)
{
	int i = fd_slot(fd);

	if (i < 0) {
		errno = EBADF;
		return -1;
	}

	size_t avail = disk_size - fds[i].pos;
	size_t n = MIN(count, avail);

	memcpy(buf, disk_ram + fds[i].pos, n);
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

	size_t space = disk_size - fds[i].pos;
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

	off_t base;

	switch (whence) {
	case SEEK_SET: base = 0; break;
	case SEEK_CUR: base = (off_t)fds[i].pos; break;
	case SEEK_END: base = (off_t)disk_size; break;
	default:
		errno = EINVAL;
		return (off_t)-1;
	}

	off_t target = base + offset;

	if (target < 0 || target > (off_t)disk_size) {
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
	st->st_size = (off_t)disk_size;
	st->st_mode = S_IFREG | 0644;
	return 0;
}

int stat(const char *path, struct stat *st)
{
	if (!is_image(path) || !disk_ready()) {
		errno = ENOENT;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = (off_t)disk_size;
	st->st_mode = S_IFREG | 0644;
	return 0;
}

int access(const char *path, int mode)
{
	(void)mode;
	if (!is_image(path) || !disk_ready()) {
		errno = ENOENT;
		return -1;
	}
	return 0;
}
