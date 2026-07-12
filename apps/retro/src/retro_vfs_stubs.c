/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- link stubs for the libretro-common VFS
 * surface the 2048 core references from its save-file path.
 *
 * The env callback answers GET_SAVE_DIRECTORY with false, so
 * read_save_file()/write_save_file() bail out before touching any of
 * these; they exist only to satisfy the linker without dragging the
 * POSIX-bound vfs_implementation.c / file_stream.c into the build.
 * Real persistence (SD /SD:/saves) is an M4 item.
 */

#include <stddef.h>

#include <streams/file_stream.h>
#include <file/file_path.h>

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
	(void)path;
	(void)mode;
	(void)hints;
	return NULL;
}

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
	(void)stream;
	(void)offset;
	(void)seek_position;
	return -1;
}

int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
	(void)stream;
	(void)data;
	(void)len;
	return -1;
}

int64_t filestream_write(RFILE *stream, const void *data, int64_t len)
{
	(void)stream;
	(void)data;
	(void)len;
	return -1;
}

int64_t filestream_tell(RFILE *stream)
{
	(void)stream;
	return -1;
}

int filestream_close(RFILE *stream)
{
	(void)stream;
	return -1;
}

void filestream_vfs_init(const struct retro_vfs_interface_info *vfs_info)
{
	(void)vfs_info;
}

size_t fill_pathname_join(char *out_path, const char *dir,
			  const char *path, size_t size)
{
	(void)dir;
	(void)path;
	if (out_path && size > 0) {
		out_path[0] = '\0';
	}
	return 0;
}

bool path_is_valid(const char *path)
{
	(void)path;
	return false;
}
