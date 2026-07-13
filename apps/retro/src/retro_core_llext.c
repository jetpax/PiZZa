/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- llext core binder (M2, the bridge).
 *
 * Loads the core as a relocatable llext and resolves the 25 retro_*
 * entry points into the dispatch table (CONFIG_LLEXT_IMPORT_ALL_GLOBALS
 * exposes them without any core-side EXPORT annotation, so the core
 * tree stays untouched). Two sources:
 *   CONFIG_RETRO_CORE_LLEXT_EMBED -- blob .incbin'd into rodata, buf
 *     loader (sim: qemu has no SD).
 *   CONFIG_RETRO_CORE_LLEXT_FS    -- CONFIG_RETRO_CORE_LLEXT_PATH on
 *     the SD FAT, fs loader; mounts /SD: if nothing else has yet.
 *
 * unbind() runs llext_teardown + llext_unload: every heap region is
 * refcount-freed, so bind/unbind cycles return the llext heap to
 * baseline -- the M2 gate.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/buf_loader.h>
#include <zephyr/llext/fs_loader.h>
#ifdef CONFIG_RETRO_CORE_LLEXT_FS
#include <zephyr/fs/fs.h>
#include <stdlib.h>
#endif
#include <string.h>

#include "retro_core.h"

LOG_MODULE_REGISTER(retro_core, CONFIG_RETRO_LOG_LEVEL);

static struct llext *core_ext;

#ifdef CONFIG_RETRO_CORE_LLEXT_FS
#include "retro_storage.h"

/* The path to load. The launcher menu overrides it per selection via
 * retro_core_set_path(); empty = the build-time default.
 */
static char fs_path[128];

void retro_core_set_path(const char *path)
{
	if (path != NULL) {
		strncpy(fs_path, path, sizeof(fs_path) - 1);
		fs_path[sizeof(fs_path) - 1] = '\0';
	}
}

/* Read the whole .llext into a RAM buffer (caller frees). The fs loader
 * seek+reads per section/symbol/relocation, and on the SD card each op
 * pays FATFS + card latency -- ~1 minute for Doom's 1.1 MB core. One
 * sequential slurp here, then a TEMPORARY buf loader over RAM, cuts that
 * to a couple of seconds (the loader peeks RAM instead of hitting the
 * card thousands of times).
 */
static void *slurp_llext(const char *path, size_t *out_len)
{
	struct fs_dirent st;
	struct fs_file_t f;
	uint8_t *buf;
	size_t off = 0;

	if (fs_stat(path, &st) != 0 || st.type != FS_DIR_ENTRY_FILE) {
		LOG_ERR("llext: stat %s failed", path);
		return NULL;
	}
	buf = malloc(st.size);
	if (buf == NULL) {
		LOG_ERR("llext: alloc %zu for %s failed", (size_t)st.size, path);
		return NULL;
	}
	fs_file_t_init(&f);
	if (fs_open(&f, path, FS_O_READ) != 0) {
		LOG_ERR("llext: open %s failed", path);
		free(buf);
		return NULL;
	}
	while (off < (size_t)st.size) {
		ssize_t rd = fs_read(&f, buf + off, (size_t)st.size - off);

		if (rd <= 0) {
			break;
		}
		off += (size_t)rd;
	}
	fs_close(&f);

	if (off != (size_t)st.size) {
		LOG_ERR("llext: short read %s (%zu/%zu)", path, off,
			(size_t)st.size);
		free(buf);
		return NULL;
	}
	*out_len = (size_t)st.size;
	return buf;
}
#else
extern const uint8_t retro_core_blob_start[];
extern const uint8_t retro_core_blob_end[];

void retro_core_set_path(const char *path)
{
	(void)path;
}
#endif

static const void *find_sym_checked(const char *name, bool *ok)
{
	const void *p = llext_find_sym(&core_ext->exp_tab, name);

	if (!p) {
		LOG_ERR("llext: core does not export %s", name);
		*ok = false;
	}
	return p;
}

int retro_core_bind(struct retro_core_api *api)
{
	int rc;
	struct llext_load_param ldr_parm = LLEXT_LOAD_PARAM_DEFAULT;

#ifdef CONFIG_RETRO_CORE_LLEXT_FS
	rc = retro_storage_mount();
	if (rc != 0) {
		return rc;
	}

	const char *path = fs_path[0] ? fs_path : CONFIG_RETRO_CORE_LLEXT_PATH;

	LOG_INF("llext: loading %s", path);

	size_t blob_len = 0;
	void *fs_buf = slurp_llext(path, &blob_len);

	if (fs_buf == NULL) {
		return -EIO;
	}
	struct llext_buf_loader buf_ldr =
		LLEXT_TEMPORARY_BUF_LOADER(fs_buf, blob_len);
	struct llext_loader *ldr = &buf_ldr.loader;
#else
	size_t blob_len = retro_core_blob_end - retro_core_blob_start;
	struct llext_buf_loader buf_ldr =
		LLEXT_BUF_LOADER(retro_core_blob_start, blob_len);
	struct llext_loader *ldr = &buf_ldr.loader;

	LOG_INF("llext: loading embedded core (%zu bytes)", blob_len);
#endif

	rc = llext_load(ldr, "core", &core_ext, &ldr_parm);

#ifdef CONFIG_RETRO_CORE_LLEXT_FS
	/* TEMPORARY storage: llext has copied every region into its heap, so
	 * nothing points back into the slurp buffer -- free it now.
	 */
	free(fs_buf);
#endif

	if (rc != 0) {
		LOG_ERR("llext: load failed (%d)", rc);
		core_ext = NULL;
		return rc;
	}

	bool ok = true;

#define BIND(field, name)						\
	api->field = (__typeof__(api->field))				\
		find_sym_checked("retro_" name, &ok)

	BIND(set_environment,            "set_environment");
	BIND(set_video_refresh,          "set_video_refresh");
	BIND(set_audio_sample,           "set_audio_sample");
	BIND(set_audio_sample_batch,     "set_audio_sample_batch");
	BIND(set_input_poll,             "set_input_poll");
	BIND(set_input_state,            "set_input_state");
	BIND(init,                       "init");
	BIND(deinit,                     "deinit");
	BIND(api_version,                "api_version");
	BIND(get_system_info,            "get_system_info");
	BIND(get_system_av_info,         "get_system_av_info");
	BIND(set_controller_port_device, "set_controller_port_device");
	BIND(reset,                      "reset");
	BIND(run,                        "run");
	BIND(serialize_size,             "serialize_size");
	BIND(serialize,                  "serialize");
	BIND(unserialize,                "unserialize");
	BIND(cheat_reset,                "cheat_reset");
	BIND(cheat_set,                  "cheat_set");
	BIND(load_game,                  "load_game");
	BIND(load_game_special,          "load_game_special");
	BIND(unload_game,                "unload_game");
	BIND(get_region,                 "get_region");
	BIND(get_memory_data,            "get_memory_data");
	BIND(get_memory_size,            "get_memory_size");
#undef BIND

	if (!ok) {
		retro_core_unbind();
		return -ENOENT;
	}

	/* Run .preinit_array/.init_array (no-op for C cores; C++ cores
	 * need their static constructors before any entry point).
	 */
	rc = llext_bringup(core_ext);
	if (rc != 0) {
		LOG_ERR("llext: bringup failed (%d)", rc);
		retro_core_unbind();
		return rc;
	}

	LOG_INF("llext: core bound (%u exported symbols)",
		(unsigned int)core_ext->exp_tab.sym_cnt);
	return 0;
}

void retro_core_unbind(void)
{
	if (!core_ext) {
		return;
	}

	int rc = llext_teardown(core_ext);

	if (rc != 0) {
		LOG_WRN("llext: teardown rc=%d", rc);
	}

	rc = llext_unload(&core_ext);
	if (rc != 0) {
		LOG_WRN("llext: unload rc=%d", rc);
	}
	core_ext = NULL;
}
