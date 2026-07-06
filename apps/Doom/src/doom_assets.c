/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- embedded IWAD store (work-order §5).
 *
 * The shareware WAD is linked into .rodata (doom_pack.S, .incbin); this
 * file serves it through a read-only POSIX fd layer, so w_file_stdc.c's
 * fopen/fread/fseek (funnelled by picolibc tinystdio through open/read/
 * lseek/close/fstat) resolve here with no filesystem on the boot path
 * and the Doom tree untouched.
 *
 * There is exactly one backing file -- the IWAD -- so any open of a
 * "*.wad" path returns it. Everything else is ENOENT (Doom falls back
 * to built-in defaults for config), and writes fail EROFS (save and
 * screenshot attempts are reported by the game and ignored). The
 * directory / write-side stubs live in the shared shim_posix.c.
 */

#include <zephyr/kernel.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern const uint8_t doom_wad_start[];
extern const uint8_t doom_wad_end[];

#define DOOM_FD_BASE 3
#define DOOM_FD_MAX  4

static struct {
	size_t pos;
	bool used;
} fds[DOOM_FD_MAX];

static size_t wad_size(void)
{
	return (size_t)(doom_wad_end - doom_wad_start);
}

static bool is_wad(const char *path)
{
	if (path == NULL) {
		return false;
	}

	size_t n = strlen(path);

	return n >= 4 && strcasecmp(path + n - 4, ".wad") == 0;
}

int open(const char *path, int flags, ...)
{
	if ((flags & O_ACCMODE) != O_RDONLY) {
		errno = EROFS;
		return -1;
	}
	if (!is_wad(path) || wad_size() == 0) {
		errno = ENOENT;
		return -1;
	}

	for (int i = 0; i < DOOM_FD_MAX; i++) {
		if (!fds[i].used) {
			fds[i].pos = 0;
			fds[i].used = true;
			return DOOM_FD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

static int fd_slot(int fd)
{
	int i = fd - DOOM_FD_BASE;

	if (i < 0 || i >= DOOM_FD_MAX || !fds[i].used) {
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

	size_t avail = wad_size() - fds[i].pos;
	size_t n = MIN(count, avail);

	memcpy(buf, doom_wad_start + fds[i].pos, n);
	fds[i].pos += n;
	return (ssize_t)n;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(count);
	errno = (fd_slot(fd) < 0) ? EBADF : EROFS;
	return -1;
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
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = (off_t)fds[i].pos;
		break;
	case SEEK_END:
		base = (off_t)wad_size();
		break;
	default:
		errno = EINVAL;
		return (off_t)-1;
	}

	off_t target = base + offset;

	if (target < 0 || target > (off_t)wad_size()) {
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
	st->st_size = (off_t)wad_size();
	st->st_mode = S_IFREG | 0444;
	return 0;
}

int stat(const char *path, struct stat *st)
{
	if (!is_wad(path) || wad_size() == 0) {
		errno = ENOENT;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = (off_t)wad_size();
	st->st_mode = S_IFREG | 0444;
	return 0;
}

int access(const char *path, int mode)
{
	if (!is_wad(path) || wad_size() == 0) {
		errno = ENOENT;
		return -1;
	}
	if ((mode & W_OK) != 0) {
		errno = EROFS;
		return -1;
	}
	return 0;
}

int remove(const char *path)
{
	ARG_UNUSED(path);
	errno = EROFS;
	return -1;
}
