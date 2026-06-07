/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Engine -> Zephyr glue:
 *   - __wrap_file_exists / __wrap_file_load / __wrap_file_store
 *     replace utils.c's libc-backed versions (no fopen() routing to
 *     FATFS in this image). Paths NOT starting with '/' are
 *     transparently prefixed with the SD mount root so the engine's
 *     "wipeout/track01/foo.tim" idiom maps to "/SD:/wipeout/track01/foo.tim".
 *   - pizza_capture_frame() writes a finished backbuffer to /SD:/ as
 *     a PPM (P6 raw RGB) for the M5 frame-capture deliverable. The
 *     frame is byte-swap-corrected so the saved file is plain RGB
 *     regardless of when in the present cycle the capture armed.
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ff.h>

/* Engine headers for rgba_t + mem_temp_alloc. */
#include "types.h"
#include "mem.h"

/* CONFIG_FS_FATFS_MULTI_PARTITION requires an app-provided VolToPart[]
 * mapping logical drive number -> { physical drive, partition number }.
 * Both volumes share physical drive 0 (the SDHost disk_access "SD"):
 *   - logical 0 / "/SD:"   -> partition 1 (PINN RECOVERY FAT)
 *   - logical 1 / "/DATA:" -> partition 3 (manually-added FAT for wipeout)
 * Partition number 0 means "auto-detect first FAT" -- we want explicit
 * indices so FatFs doesn't fall back to partition 1 for both.
 */
PARTITION VolToPart[FF_VOLUMES] = {
	[0] = {0, 1},   /* /SD:   -> physical 0, partition 1 (PINN RECOVERY) */
	[1] = {0, 2},   /* /DATA: -> physical 0, partition 2 (manually created FAT32 for wipeout assets) */
};

/* Assets live on the second partition (the manually-created one);
 * save data + user state stays on the small PINN RECOVERY FAT so the
 * user's PINN bootloader keeps any state it persists alongside it.
 */
#define SD_MOUNT_POINT   "/SD:"
#define DATA_MOUNT_POINT "/DATA:"
#define ASSET_PREFIX     DATA_MOUNT_POINT

static void pizza_resolve(char *out, size_t out_sz, const char *path)
{
	if (path[0] == '/') {
		strncpy(out, path, out_sz - 1);
		out[out_sz - 1] = '\0';
	} else {
		/* Relative paths are engine asset references like
		 * "wipeout/track01/foo.tim" -- always resolved against the
		 * assets partition. Save-data callers in platform_zephyr.c
		 * pre-build paths under /SD:/userdata/ and arrive here with
		 * a leading '/', so the branch above handles them.
		 */
		snprintf(out, out_sz, "%s/%s", ASSET_PREFIX, path);
	}
	/* ELM-CHAN FatFs `f_stat` rejects trailing separators with
	 * FR_INVALID_NAME -- so `file_exists("wipeout/track01/")` (an
	 * engine idiom for checking that a directory is present) would
	 * spuriously fail. Strip a single trailing slash; keep the
	 * leading mount-point slash even on `/`-only paths.
	 */
	size_t n = strlen(out);
	if (n > 1 && out[n - 1] == '/') {
		out[n - 1] = '\0';
	}
}

bool __wrap_file_exists(const char *path)
{
	char buf[256];
	struct fs_dirent ent;

	pizza_resolve(buf, sizeof(buf), path);
	return fs_stat(buf, &ent) == 0;
}

uint8_t *__wrap_file_load(const char *path, uint32_t *bytes_read)
{
	char buf[256];
	struct fs_file_t f;
	int rc;

	pizza_resolve(buf, sizeof(buf), path);

	fs_file_t_init(&f);
	rc = fs_open(&f, buf, FS_O_READ);
	if (rc < 0) {
		printk("[wipeout] fs_open(%s): %d\n", buf, rc);
		if (bytes_read) {
			*bytes_read = 0;
		}
		return NULL;
	}

	off_t size = fs_tell(&f);
	rc = fs_seek(&f, 0, FS_SEEK_END);
	if (rc < 0) {
		fs_close(&f);
		if (bytes_read) {
			*bytes_read = 0;
		}
		return NULL;
	}
	size = fs_tell(&f);
	(void)fs_seek(&f, 0, FS_SEEK_SET);

	if (size <= 0) {
		fs_close(&f);
		if (bytes_read) {
			*bytes_read = 0;
		}
		return NULL;
	}

	uint8_t *bytes = (uint8_t *)mem_temp_alloc((uint32_t)size);

	if (!bytes) {
		printk("[wipeout] mem_temp_alloc(%lld) failed for %s\n",
		       (long long)size, buf);
		fs_close(&f);
		if (bytes_read) {
			*bytes_read = 0;
		}
		return NULL;
	}

	ssize_t got = fs_read(&f, bytes, (size_t)size);

	fs_close(&f);
	if (got != size) {
		printk("[wipeout] short read on %s: %lld of %lld\n",
		       buf, (long long)got, (long long)size);
		mem_temp_free(bytes);
		if (bytes_read) {
			*bytes_read = 0;
		}
		return NULL;
	}
	if (bytes_read) {
		*bytes_read = (uint32_t)size;
	}
	return bytes;
}

uint32_t __wrap_file_store(const char *path, void *bytes, int32_t len)
{
	char buf[256];
	struct fs_file_t f;
	int rc;

	pizza_resolve(buf, sizeof(buf), path);

	fs_file_t_init(&f);
	rc = fs_open(&f, buf, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc < 0) {
		printk("[wipeout] fs_open(%s for write): %d\n", buf, rc);
		return 0;
	}
	ssize_t wr = fs_write(&f, bytes, (size_t)len);

	fs_close(&f);
	if (wr != len) {
		printk("[wipeout] short write on %s: %lld of %d\n",
		       buf, (long long)wr, (int)len);
		return 0;
	}
	return (uint32_t)len;
}

/* ------------------------------------------------------------------ *
 *  Frame capture -> /SD:/wipeout-frame.ppm
 *
 *  Called from the game loop right after pizza_present() byte-swaps
 *  the backbuffer. PPM (P6) is a 17-byte ASCII header + raw bytes;
 *  bytes per pixel = 3 (RGB), no alpha. The buffer at this point is
 *  B,G,R,A in memory (post-swap, pre-next-clear). We emit R,G,B by
 *  pulling bytes[2],[1],[0] for each pixel.
 * ------------------------------------------------------------------ */

#define CAPTURE_PATH "/DATA:/wipeout-frame.ppm"

int pizza_capture_frame(const rgba_t *buf, int w, int h)
{
	struct fs_file_t f;
	int rc;
	char hdr[64];

	fs_file_t_init(&f);
	rc = fs_open(&f, CAPTURE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc < 0) {
		printk("[wipeout] capture: fs_open(%s) -> %d\n", CAPTURE_PATH, rc);
		return rc;
	}

	int hdr_len = snprintf(hdr, sizeof(hdr), "P6\n%d %d\n255\n", w, h);

	(void)fs_write(&f, hdr, (size_t)hdr_len);

	/* Row buffer: 3 bytes/px x width. 320 px -> 960 bytes. */
	uint8_t row[3 * 1024];
	const uint8_t *p = (const uint8_t *)buf;

	for (int y = 0; y < h; y++) {
		uint8_t *o = row;
		for (int x = 0; x < w; x++, p += 4) {
			/* Post-byteswap layout in pizza_backbuf: [B,G,R,A].
			 * Emit R, G, B (drop A).
			 */
			o[0] = p[2]; /* R */
			o[1] = p[1]; /* G */
			o[2] = p[0]; /* B */
			o += 3;
		}
		(void)fs_write(&f, row, (size_t)(3 * w));
	}

	fs_close(&f);
	printk("[wipeout] capture: %d x %d written to %s\n", w, h, CAPTURE_PATH);
	return 0;
}
