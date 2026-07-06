/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- embedded IWAD store (work-order §5).
 *
 * The shareware WAD is linked into .rodata (doom_pack.S, .incbin); this
 * file serves it through a POSIX fd layer, so w_file_stdc.c's fopen/fread/
 * fseek (funnelled by picolibc tinystdio through open/read/lseek/close/
 * fstat) resolve here with no filesystem on the boot path and the Doom
 * tree untouched.
 *
 * Two backing files: the read-only IWAD (any "*.wad" path), and -- when
 * OPL music is built -- ONE writable RAM temp file, "doom.mid". The OPL
 * music path round-trips each converted song through M_TempFile("doom.mid")
 * -> M_WriteFile -> MIDI_LoadFile; the fork's M_WriteFile and Chocolate's
 * MIDI_LoadFile both funnel through open()/read()/write(), so backing that
 * one path with RAM here makes the write and read see the same bytes.
 * Everything else is ENOENT; other writes fail EROFS.
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

enum fd_kind {
	FD_FREE = 0,
	FD_WAD,
	FD_TEMP,
};

static struct {
	size_t pos;
	enum fd_kind kind;
} fds[DOOM_FD_MAX];

/* Writable RAM temp file (the OPL MUS->MIDI round-trip). A Doom MIDI is a
 * few KB; 96 KiB matches i_oplmusic's MAXMIDLENGTH ceiling.
 */
#define MID_MAX (96 * 1024)
static uint8_t mid_buf[MID_MAX];
static size_t mid_len;

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

static bool is_temp(const char *path)
{
	if (path == NULL) {
		return false;
	}

	size_t n = strlen(path);

	return n >= 8 && strcasecmp(path + n - 8, "doom.mid") == 0;
}

int open(const char *path, int flags, ...)
{
	bool wr = (flags & O_ACCMODE) != O_RDONLY;
	enum fd_kind kind;

	if (is_temp(path)) {
		kind = FD_TEMP;
		if (wr) {
			mid_len = 0; /* truncate on write-open */
		}
	} else if (wr) {
		errno = EROFS;
		return -1;
	} else if (!is_wad(path) || wad_size() == 0) {
		errno = ENOENT;
		return -1;
	} else {
		kind = FD_WAD;
	}

	for (int i = 0; i < DOOM_FD_MAX; i++) {
		if (fds[i].kind == FD_FREE) {
			fds[i].pos = 0;
			fds[i].kind = kind;
			return DOOM_FD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

static int fd_slot(int fd)
{
	int i = fd - DOOM_FD_BASE;

	if (i < 0 || i >= DOOM_FD_MAX || fds[i].kind == FD_FREE) {
		return -1;
	}
	return i;
}

static size_t fd_size(int i)
{
	return (fds[i].kind == FD_TEMP) ? mid_len : wad_size();
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

	const uint8_t *src = (fds[i].kind == FD_TEMP) ? mid_buf : doom_wad_start;
	size_t avail = fd_size(i) - fds[i].pos;
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
	if (fds[i].kind != FD_TEMP) {
		errno = EROFS;
		return -1;
	}

	size_t space = MID_MAX - fds[i].pos;
	size_t n = MIN(count, space);

	memcpy(mid_buf + fds[i].pos, buf, n);
	fds[i].pos += n;
	if (fds[i].pos > mid_len) {
		mid_len = fds[i].pos;
	}
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

	off_t fsize = (off_t)fd_size(i);
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
	st->st_size = (off_t)fd_size(i);
	st->st_mode = S_IFREG | ((fds[i].kind == FD_TEMP) ? 0644 : 0444);
	return 0;
}

int stat(const char *path, struct stat *st)
{
	bool temp = is_temp(path);

	if (!temp && (!is_wad(path) || wad_size() == 0)) {
		errno = ENOENT;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = temp ? (off_t)mid_len : (off_t)wad_size();
	st->st_mode = S_IFREG | (temp ? 0644 : 0444);
	return 0;
}

int access(const char *path, int mode)
{
	if (is_temp(path)) {
		return 0;
	}
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
	if (is_temp(path)) {
		mid_len = 0;
		return 0;
	}
	errno = EROFS;
	return -1;
}
