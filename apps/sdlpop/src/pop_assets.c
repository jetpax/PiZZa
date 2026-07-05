/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDLPoP port -- embedded asset store (work-order §4).
 *
 * The pack built by tools/pack_assets.py is linked into .rodata; this
 * file implements the read-only POSIX fd layer over it. picolibc's
 * tinystdio (fopen/fread/fseek/fileno) funnels through open/read/
 * lseek/close, so the whole game "filesystem" resolves here with the
 * game tree untouched and no FS driver on the critical path.
 *
 * Path normalization: the game asks for CWD-relative paths
 * ("data/KID/res401.png") when access() says they exist -- which it
 * does here -- but may also try exe-dir-prefixed variants
 * ("prince/data/..."); the resolver retries from the first "data/"
 * component.
 *
 * Writes (quicksave, replays, screenshots) fail with EROFS -- the
 * game reports and continues. Backing saves with Zephyr fs_* on the
 * SD card is a later, additive step.
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

extern const uint8_t pop_pack_start[];
extern const uint8_t pop_pack_end[];

static const struct pack_entry *pack_entries;
static uint32_t pack_count;
static bool pack_ready;

#define POP_FD_BASE 3
#define POP_FD_MAX  16

static struct {
	const struct pack_entry *entry;
	size_t pos;
	bool used;
} fds[POP_FD_MAX];

static void pack_init(void)
{
	if (pack_ready) {
		return;
	}
	if (memcmp(pop_pack_start, "POPPACK1", 8) != 0) {
		printk("[sdlpop] asset pack magic mismatch!\n");
		pack_count = 0;
	} else {
		pack_count = *(const uint32_t *)(pop_pack_start + 8);
		pack_entries = (const struct pack_entry *)(pop_pack_start + 12);
		printk("[sdlpop] asset pack: %u entries, %u bytes\n",
		       pack_count, (uint32_t)(pop_pack_end - pop_pack_start));
	}
	pack_ready = true;
}

static const char *entry_path(const struct pack_entry *e)
{
	return (const char *)pop_pack_start + e->path_off;
}

static const struct pack_entry *pack_lookup_exact(const char *path)
{
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

static const char *normalize(const char *path, char *buf, size_t buflen)
{
	strncpy(buf, path, buflen - 1);
	buf[buflen - 1] = '\0';

	const char *p = buf;

	while (p[0] == '.' && p[1] == '/') {
		p += 2;
	}
	while (p[0] == '/') {
		p++;
	}
	return p;
}

static const struct pack_entry *pack_lookup(const char *path)
{
	pack_init();
	if (path == NULL || pack_count == 0) {
		return NULL;
	}

	char norm[256];
	const char *p = normalize(path, norm, sizeof(norm));
	const struct pack_entry *e = pack_lookup_exact(p);

	if (e != NULL) {
		return e;
	}

	/* exe-dir-prefixed variants: retry from the first "data/" */
	const char *d = strstr(p, "data/");

	if (d != NULL && d != p) {
		return pack_lookup_exact(d);
	}
	return NULL;
}

/* Directory probe: true when any entry lives under path/ (the game
 * stats data/KID etc. to decide the loose-resource layout exists).
 */
static bool pack_lookup_dir_exact(const char *path)
{
	size_t len = strlen(path);

	if (len == 0) {
		return false;
	}

	uint32_t lo = 0, hi = pack_count;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (strcmp(entry_path(&pack_entries[mid]), path) < 0) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (lo == pack_count) {
		return false;
	}

	const char *cand = entry_path(&pack_entries[lo]);

	return strncmp(cand, path, len) == 0 && cand[len] == '/';
}

static bool pack_lookup_dir(const char *path)
{
	pack_init();
	if (path == NULL || pack_count == 0) {
		return false;
	}

	char norm[256];
	const char *p = normalize(path, norm, sizeof(norm));

	if (pack_lookup_dir_exact(p)) {
		return true;
	}

	const char *d = strstr(p, "data/");

	return (d != NULL && d != p) ? pack_lookup_dir_exact(d) : false;
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

	for (int i = 0; i < POP_FD_MAX; i++) {
		if (!fds[i].used) {
			fds[i].entry = e;
			fds[i].pos = 0;
			fds[i].used = true;
			return POP_FD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

static int fd_slot(int fd)
{
	int i = fd - POP_FD_BASE;

	if (i < 0 || i >= POP_FD_MAX || !fds[i].used) {
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

	memcpy(buf, pop_pack_start + e->data_off + fds[i].pos, n);
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

	if (e != NULL) {
		memset(st, 0, sizeof(*st));
		st->st_size = (off_t)e->size;
		st->st_mode = S_IFREG | 0444;
		return 0;
	}
	if (pack_lookup_dir(path)) {
		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFDIR | 0555;
		return 0;
	}
	errno = ENOENT;
	return -1;
}

int access(const char *path, int mode)
{
	if (pack_lookup(path) == NULL && !pack_lookup_dir(path)) {
		errno = ENOENT;
		return -1;
	}
	if ((mode & W_OK) != 0) {
		errno = EROFS;
		return -1;
	}
	return 0;
}

/* Direct lookup for shim-internal users (IMG_Load). */
const void *pop_asset_find(const char *path, size_t *size)
{
	const struct pack_entry *e = pack_lookup(path);

	if (e == NULL) {
		return NULL;
	}
	if (size != NULL) {
		*size = e->size;
	}
	return pop_pack_start + e->data_off;
}
