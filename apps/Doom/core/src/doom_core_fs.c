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
	bool is_temp; /* RAM-backed doom.mid, not an fs file */
	size_t pos;   /* cursor for the temp file */
};

/* OPL music round-trips each song through a "doom.mid" temp file
 * (i_oplmusic: MUS -> mus2mid -> M_WriteFile -> MIDI_LoadFile), and both
 * the write and the read funnel through this stdio layer. The core's fs is
 * the real SD card and a relative temp path has no mount point, so back
 * that ONE path with RAM -- the write and the read see the same bytes.
 * Mirrors the app's doom_assets.c. 96 KiB = i_oplmusic's MAXMIDLENGTH.
 */
#define MID_MAX (96 * 1024)
static uint8_t mid_buf[MID_MAX];
static size_t mid_len;

static bool is_temp(const char *path)
{
	size_t n, k;

	if (path == NULL) {
		return false;
	}
	n = strlen(path);
	k = strlen("doom.mid");
	if (n < k) {
		return false;
	}
	path += n - k;
	for (size_t i = 0; i < k; i++) {
		char a = path[i];
		char b = "doom.mid"[i];

		if (a >= 'A' && a <= 'Z') {
			a += 'a' - 'A';
		}
		if (a != b) {
			return false;
		}
	}
	return true;
}

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

	if (is_temp(path)) {
		cf->is_temp = true;
		cf->pos = 0;
		if (mode[0] == 'w') {
			mid_len = 0; /* truncate on write-open */
		}
		return (FILE *)cf;
	}

	cf->is_temp = false;
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
	if (!cf->is_temp) {
		fs_close(&cf->f);
	}
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
	if (cf->is_temp) {
		size_t want = size * nmemb;
		size_t avail = (cf->pos < mid_len) ? mid_len - cf->pos : 0;
		size_t n = (want < avail) ? want : avail;

		memcpy(ptr, mid_buf + cf->pos, n);
		cf->pos += n;
		return n / size;
	}
	rd = fs_read(&cf->f, ptr, size * nmemb);
	if (rd <= 0) {
		return 0;
	}
	return (size_t)rd / size;
}

/* Chocolate midifile.c parses the MIDI stream one byte at a time through
 * fgetc (ReadByte). fgetc is NOT one of the six functions this layer
 * overrides, so without this it would bind to the frontend's picolibc
 * fgetc, which would read our struct core_file as a picolibc FILE and
 * dereference garbage. Define it here over the same handle. (Doom never
 * reads stdin, so a core-wide fgetc is safe.)
 */
int fgetc(FILE *stream)
{
	struct core_file *cf = (struct core_file *)stream;
	unsigned char c;

	if (cf == NULL) {
		return EOF;
	}
	if (cf->is_temp) {
		if (cf->pos >= mid_len) {
			return EOF;
		}
		return (int)mid_buf[cf->pos++];
	}
	if (fs_read(&cf->f, &c, 1) != 1) {
		return EOF;
	}
	return (int)c;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	struct core_file *cf = (struct core_file *)stream;
	ssize_t wr;

	if (cf == NULL || size == 0) {
		return 0;
	}
	if (cf->is_temp) {
		size_t want = size * nmemb;
		size_t space = (cf->pos < MID_MAX) ? MID_MAX - cf->pos : 0;
		size_t n = (want < space) ? want : space;

		memcpy(mid_buf + cf->pos, ptr, n);
		cf->pos += n;
		if (cf->pos > mid_len) {
			mid_len = cf->pos;
		}
		return n / size;
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
	if (cf->is_temp) {
		long base;
		long np;

		switch (whence) {
		case SEEK_SET:
			base = 0;
			break;
		case SEEK_CUR:
			base = (long)cf->pos;
			break;
		case SEEK_END:
			base = (long)mid_len;
			break;
		default:
			return -1;
		}
		np = base + offset;
		if (np < 0 || np > (long)MID_MAX) {
			return -1;
		}
		cf->pos = (size_t)np;
		return 0;
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
	if (cf->is_temp) {
		return (long)cf->pos;
	}
	return (long)fs_tell(&cf->f);
}
