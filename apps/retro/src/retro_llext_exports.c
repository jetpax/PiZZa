/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- symbol exports for llext cores.
 *
 * The exact undefined-symbol surface of 2048.llext (aarch64-...-nm -u),
 * exported from the frontend image so llext_load resolves them. Driven
 * empirically, NOT from grep -- extend when a new core's load log
 * reports unresolved symbols. The filestream/path stubs live in
 * retro_vfs_stubs.c and are exported here so cores share the frontend's
 * (refusing) VFS surface.
 */

#include <zephyr/llext/symbol.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <streams/file_stream.h>
#include <file/file_path.h>

/* picolibc assert plumbing (assert() with no message variant). */
extern void __assert_no_args(void);

/* string.h / stdlib.h */
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(atoi);
EXPORT_SYMBOL(malloc);
EXPORT_SYMBOL(calloc);
EXPORT_SYMBOL(free);
EXPORT_SYMBOL(rand);
EXPORT_SYMBOL(srand);

/* stdio (tinystdio funnels the FILE* globals too) */
EXPORT_SYMBOL(fprintf);
EXPORT_SYMBOL(sprintf);
EXPORT_SYMBOL(vsprintf);
EXPORT_SYMBOL(stdout);
EXPORT_SYMBOL(stderr);

/* libm / time / assert */
EXPORT_SYMBOL(cos);
EXPORT_SYMBOL(time);
EXPORT_SYMBOL(__assert_no_args);

/* frontend VFS stubs (retro_vfs_stubs.c) */
EXPORT_SYMBOL(filestream_open);
EXPORT_SYMBOL(filestream_close);
EXPORT_SYMBOL(filestream_read);
EXPORT_SYMBOL(filestream_write);
EXPORT_SYMBOL(filestream_seek);
EXPORT_SYMBOL(filestream_tell);
EXPORT_SYMBOL(filestream_vfs_init);
EXPORT_SYMBOL(fill_pathname_join);
EXPORT_SYMBOL(path_is_valid);
