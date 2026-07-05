/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- POSIX odds and ends SDLPoP expects from a
 * hosted libc. The read-only fd layer (open/read/lseek/close/stat)
 * lives in pop_assets.c over the embedded pack; this file carries
 * the write-side and directory calls, which fail cleanly (read-only
 * store, no mods/replays enumeration).
 */

#include <zephyr/kernel.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

int unlink(const char *path)
{
	ARG_UNUSED(path);
	errno = EROFS;
	return -1;
}

int mkdir(const char *path, mode_t mode)
{
	ARG_UNUSED(path);
	ARG_UNUSED(mode);
	errno = EROFS;
	return -1;
}

/* The game chdirs to its root at startup; all paths are relative and
 * the asset resolver owns them, so pretending success is correct.
 */
int chdir(const char *path)
{
	ARG_UNUSED(path);
	return 0;
}

/* ── directory listing: nothing to enumerate ─────────────────── */

DIR *opendir(const char *name)
{
	ARG_UNUSED(name);
	return NULL;
}

struct dirent *readdir(DIR *dirp)
{
	ARG_UNUSED(dirp);
	return NULL;
}

int closedir(DIR *dirp)
{
	ARG_UNUSED(dirp);
	return -1;
}
