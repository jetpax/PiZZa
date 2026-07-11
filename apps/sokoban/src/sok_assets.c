/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sokoban port -- embedded asset store.
 *
 * The pack built by tools/pack_assets.py is linked into .rodata; this
 * file implements the read-only POSIX fd layer over it (picolibc's
 * tinystdio funnels fopen/fread through open/read), plus the direct
 * s2s_asset_find lookup the shim's IMG/TTF/TMX loaders use. Same
 * recipe as SDLPoP's pop_assets.c; sokoban asks for pack-root-relative
 * paths ("Font/ARCADE_N.TTF", "Maps/Original/level_001.tmx") so the
 * exe-dir retry heuristics are gone.
 *
 * The game only opens files to probe level existence (fopen/fclose in
 * InitLevel); writes fail EROFS.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct pack_entry {
	uint32_t path_off;
	uint32_t data_off;
	uint32_t size;
} __packed;

extern const uint8_t sok_pack_start[];
extern const uint8_t sok_pack_end[];

static const struct pack_entry *pack_entries;
static uint32_t pack_count;
static bool pack_ready;

#define SOK_FD_BASE 3
#define SOK_FD_MAX  8

static struct {
	const struct pack_entry *entry;
	size_t pos;
	bool used;
} fds[SOK_FD_MAX];

static void pack_init(void)
{
	if (pack_ready) {
		return;
	}
	if (memcmp(sok_pack_start, "SOKPACK1", 8) != 0) {
		printk("[sokoban] asset pack magic mismatch!\n");
		pack_count = 0;
	} else {
		pack_count = *(const uint32_t *)(sok_pack_start + 8);
		pack_entries = (const struct pack_entry *)(sok_pack_start + 12);
		printk("[sokoban] asset pack: %u entries, %u bytes\n",
		       pack_count, (uint32_t)(sok_pack_end - sok_pack_start));
	}
	pack_ready = true;
}

static const char *entry_path(const struct pack_entry *e)
{
	return (const char *)sok_pack_start + e->path_off;
}

static const struct pack_entry *pack_lookup(const char *path)
{
	pack_init();
	if (path == NULL || pack_count == 0) {
		return NULL;
	}

	while (path[0] == '.' && path[1] == '/') {
		path += 2;
	}
	while (path[0] == '/') {
		path++;
	}

	uint32_t lo = 0, hi = pack_count;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		int c = strcmp(entry_path(&pack_entries[mid]), path);

		if (c == 0) {
			return &pack_entries[mid];
		}
		if (c < 0) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return NULL;
}

/* ── POSIX fd layer (picolibc tinystdio backend) ─────────────── */

int open(const char *path, int flags, ...)
{
	if ((flags & O_ACCMODE) != O_RDONLY) {
		errno = EROFS;
		return -1;
	}

	const struct pack_entry *e = pack_lookup(path);

	if (e == NULL) {
		errno = ENOENT;
		return -1;
	}

	for (int i = 0; i < SOK_FD_MAX; i++) {
		if (!fds[i].used) {
			fds[i].entry = e;
			fds[i].pos = 0;
			fds[i].used = true;
			return SOK_FD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

static int fd_slot(int fd)
{
	int i = fd - SOK_FD_BASE;

	if (i < 0 || i >= SOK_FD_MAX || !fds[i].used) {
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

	const struct pack_entry *e = fds[i].entry;
	size_t avail = e->size - fds[i].pos;
	size_t n = MIN(count, avail);

	memcpy(buf, sok_pack_start + e->data_off + fds[i].pos, n);
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

	const struct pack_entry *e = fds[i].entry;
	off_t base;

	switch (whence) {
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = (off_t)fds[i].pos;
		break;
	case SEEK_END:
		base = (off_t)e->size;
		break;
	default:
		errno = EINVAL;
		return (off_t)-1;
	}

	off_t target = base + offset;

	if (target < 0 || target > (off_t)e->size) {
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
	st->st_size = (off_t)fds[i].entry->size;
	st->st_mode = S_IFREG | 0444;
	return 0;
}

int stat(const char *path, struct stat *st)
{
	const struct pack_entry *e = pack_lookup(path);

	if (e == NULL) {
		errno = ENOENT;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = (off_t)e->size;
	st->st_mode = S_IFREG | 0444;
	return 0;
}

int access(const char *path, int mode)
{
	if (pack_lookup(path) == NULL) {
		errno = ENOENT;
		return -1;
	}
	if ((mode & W_OK) != 0) {
		errno = EROFS;
		return -1;
	}
	return 0;
}

/* Direct lookup for shim-internal users (IMG_Load, TTF_OpenFont,
 * tmx_load).
 */
const void *s2s_asset_find(const char *path, size_t *size)
{
	const struct pack_entry *e = pack_lookup(path);

	if (e == NULL) {
		return NULL;
	}
	if (size != NULL) {
		*size = e->size;
	}
	return sok_pack_start + e->data_off;
}
