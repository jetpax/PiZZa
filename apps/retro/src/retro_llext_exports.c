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

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/llext/symbol.h>
#ifdef CONFIG_FILE_SYSTEM
#include <zephyr/fs/fs.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
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

/* ── sokoban core additions (sdl2shim-over-libretro, C++) ──────── */

/* libc growth */
EXPORT_SYMBOL(cosf);
EXPORT_SYMBOL(sinf);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(printf);
EXPORT_SYMBOL(puts);
EXPORT_SYMBOL(snprintf);
EXPORT_SYMBOL(vsnprintf);

/* kernel: the glue's thread/sem/timer machinery + SDL timing. With
 * CONFIG_USERSPACE=n the k_* wrappers inline to z_impl_* calls, so
 * those are the actual imports.
 */
EXPORT_SYMBOL(printk);
EXPORT_SYMBOL(k_timer_init);
EXPORT_SYMBOL(sys_clock_cycle_get_64);
/* HW-only: with TIMER_READS_ITS_FREQUENCY_AT_RUNTIME the cycle rate
 * is this global, not a compile-time constant (qemu never links it).
 * The header extern (time_units.h) is scoped inside an inline fn, so
 * redeclare at file scope to export it. sys_clock_hw_cycles_per_sec()
 * / k_uptime / anything cycle-based reads it.
 */
#ifdef CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME
extern unsigned int z_clock_hw_cycles_per_sec;
EXPORT_SYMBOL(z_clock_hw_cycles_per_sec);
#endif
EXPORT_SYMBOL(z_impl_k_sem_init);
EXPORT_SYMBOL(z_impl_k_sem_give);
EXPORT_SYMBOL(z_impl_k_sem_take);
EXPORT_SYMBOL(z_impl_k_mutex_init);
EXPORT_SYMBOL(z_impl_k_mutex_lock);
EXPORT_SYMBOL(z_impl_k_mutex_unlock);
EXPORT_SYMBOL(z_impl_k_sleep);
EXPORT_SYMBOL(z_impl_k_thread_create);
EXPORT_SYMBOL(z_impl_k_thread_join);
EXPORT_SYMBOL(z_impl_k_thread_abort);
EXPORT_SYMBOL(z_impl_k_thread_name_set);
EXPORT_SYMBOL(z_impl_k_timer_start);
EXPORT_SYMBOL(z_impl_k_timer_stop);
EXPORT_SYMBOL(z_impl_k_uptime_ticks);

/* sdl2shim-over-libretro donations: ALL kernel objects a wrapped-app
 * core needs live HERE, in the image, never in the core. struct
 * k_thread/k_sem/k_mutex layouts depend on kernel config; the core's
 * minimal TLS-free build disagrees with this image's, so core-side
 * allocation gets overrun when the frontend kernel initializes the
 * object with its own sizeof (the M3 semaphore-zeroing incident).
 */
K_THREAD_STACK_DEFINE(s2s_lr_app_stack, CONFIG_SDL2SHIM_LIBRETRO_APP_STACK);
const size_t s2s_lr_app_stack_size = K_THREAD_STACK_SIZEOF(s2s_lr_app_stack);
struct k_thread s2s_lr_app_thread;
struct k_sem s2s_lr_sem_run;
struct k_sem s2s_lr_sem_frame;
struct k_mutex s2s_lr_audio_mutex;
EXPORT_SYMBOL(s2s_lr_app_stack);
EXPORT_SYMBOL(s2s_lr_app_stack_size);
EXPORT_SYMBOL(s2s_lr_app_thread);
EXPORT_SYMBOL(s2s_lr_sem_run);
EXPORT_SYMBOL(s2s_lr_sem_frame);
EXPORT_SYMBOL(s2s_lr_audio_mutex);

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

/* ── M5 content cores: the fs surface a core's stdio layer binds to ──
 * A need_fullpath core (fstest, Doom) defines its own fopen/fread/...
 * over these (Approach 2: an imported picolibc fopen would funnel to
 * the frontend's open(), never the core's -- so the core owns stdio and
 * calls straight through to fs_*). Exporting fs_seek/fs_tell also
 * anchors them into the image (the frontend itself only reads
 * sequentially).
 */
#ifdef CONFIG_FILE_SYSTEM
EXPORT_SYMBOL(fs_open);
EXPORT_SYMBOL(fs_close);
EXPORT_SYMBOL(fs_read);
EXPORT_SYMBOL(fs_write);
EXPORT_SYMBOL(fs_seek);
EXPORT_SYMBOL(fs_tell);
EXPORT_SYMBOL(fs_stat);
EXPORT_SYMBOL(fs_mkdir);
EXPORT_SYMBOL(fs_unlink);
#endif

/* ── Doom core additions (sdl2-doom engine libc surface) ──────────
 * The empirical set doom.llext imports that the frontend image does not
 * otherwise pull in (nm -u doom.llext minus the image's defined globals;
 * the rest -- memmove/realloc/strchr/strncpy/exit/vfprintf... -- are
 * already linked and resolve via LLEXT_IMPORT_ALL_GLOBALS). These are
 * exported for the game's config/DEH/string paths; the WAD read path
 * itself binds to the core's own fs-backed stdio (doom_core_fs.c).
 */
extern const char _ctype_b[]; /* picolibc ctype table (isX inlines) */
/* POSIX, not exposed by the default picolibc feature set here. */
extern char *strdup(const char *s);
extern int putenv(char *string);
EXPORT_SYMBOL(_ctype_b);
EXPORT_SYMBOL(atexit);
EXPORT_SYMBOL(atof);
EXPORT_SYMBOL(putenv);
EXPORT_SYMBOL(remove);
EXPORT_SYMBOL(fflush);
EXPORT_SYMBOL(fgets);
EXPORT_SYMBOL(fputc);
EXPORT_SYMBOL(putchar);
EXPORT_SYMBOL(sscanf);
EXPORT_SYMBOL(vfprintf);
EXPORT_SYMBOL(strcasecmp);
EXPORT_SYMBOL(strncasecmp);
EXPORT_SYMBOL(strdup);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(tolower);
EXPORT_SYMBOL(toupper);
/* Libc the frontend doesn't itself reference (so absent from the image
 * and unreachable via IMPORT_ALL_GLOBALS); EXPORT_SYMBOL anchors them.
 * exit/strchr the frontend never calls; realloc/strrchr/strncpy/memmove
 * it does, but export them too so the set is explicit and load-proof.
 */
EXPORT_SYMBOL(exit);
EXPORT_SYMBOL(realloc);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strncpy);
