/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom libretro core -- filesystem-backed stdio.
 *
 * The app build bakes the IWAD into rodata and serves it through a POSIX
 * fd layer (doom_assets.c). The CORE build instead reads the WAD the
 * launcher picked off /SD:/roms (need_fullpath): the glue hands the game
 * "core -iwad <path>", and Doom fopen()s that path.
 *
 * Approach 2 (see the M5 handover + the sokoban rule-4 precedent): an
 * IMPORTED picolibc fopen would funnel to the FRONTEND's open(), never
 * this core's -- so the core DEFINES the stdio surface Doom's WAD path
 * uses (fopen/fread/fseek/ftell/fclose, plus fwrite for saves/config)
 * and binds it at partial link, calling straight through to the
 * frontend's exported fs_*. A private FILE is sufficient: Doom treats
 * the handle opaquely (w_file_stdc.c / m_misc.c), and the feof/fscanf
 * loops that would touch picolibc's real FILE fields sit behind a
 * successful fopen of files that do not exist on a WAD-only card.
 *
 * Validated in isolation first by apps/retro/corefstest (fstest.llext),
 * which shares this exact layer.
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct core_file {
	struct fs_file_t f;
};

static int mode_to_flags(const char *mode)
{
	if (mode[0] == 'w') {
		return FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC;
	}
	if (mode[0] == 'a') {
		return FS_O_WRITE | FS_O_CREATE | FS_O_APPEND;
	}
	return FS_O_READ;
}

FILE *fopen(const char *path, const char *mode)
{
	struct core_file *cf = malloc(sizeof(*cf));
	int rc;

	if (cf == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	fs_file_t_init(&cf->f);
	rc = fs_open(&cf->f, path, mode_to_flags(mode));
	if (rc != 0) {
		free(cf);
		errno = (rc < 0) ? -rc : ENOENT;
		return NULL;
	}
	return (FILE *)cf;
}

int fclose(FILE *stream)
{
	struct core_file *cf = (struct core_file *)stream;

	if (cf == NULL) {
		return -1;
	}
	fs_close(&cf->f);
	free(cf);
	return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	struct core_file *cf = (struct core_file *)stream;
	ssize_t rd;

	if (cf == NULL || size == 0) {
		return 0;
	}
	rd = fs_read(&cf->f, ptr, size * nmemb);
	if (rd <= 0) {
		return 0;
	}
	return (size_t)rd / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	struct core_file *cf = (struct core_file *)stream;
	ssize_t wr;

	if (cf == NULL || size == 0) {
		return 0;
	}
	wr = fs_write(&cf->f, ptr, size * nmemb);
	if (wr <= 0) {
		return 0;
	}
	return (size_t)wr / size;
}

int fseek(FILE *stream, long offset, int whence)
{
	struct core_file *cf = (struct core_file *)stream;
	int w;

	if (cf == NULL) {
		return -1;
	}
	switch (whence) {
	case SEEK_SET:
		w = FS_SEEK_SET;
		break;
	case SEEK_CUR:
		w = FS_SEEK_CUR;
		break;
	case SEEK_END:
		w = FS_SEEK_END;
		break;
	default:
		return -1;
	}
	return fs_seek(&cf->f, offset, w) == 0 ? 0 : -1;
}

long ftell(FILE *stream)
{
	struct core_file *cf = (struct core_file *)stream;

	if (cf == NULL) {
		return -1;
	}
	return (long)fs_tell(&cf->f);
}
