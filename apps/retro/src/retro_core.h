/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- core-binding seam.
 *
 * The frontend drives a core exclusively through this dispatch table.
 * Two binders (Kconfig choice):
 *   retro_core_static.c -- link-time symbols (fused binary, M0/M1)
 *   retro_core_llext.c  -- llext-loaded core (M2+): embedded blob via
 *                          buf loader (sim) or /SD: file via fs loader
 */

#ifndef PIZZA_RETRO_CORE_H
#define PIZZA_RETRO_CORE_H

#include <libretro.h>

struct retro_core_api {
	void (*set_environment)(retro_environment_t);
	void (*set_video_refresh)(retro_video_refresh_t);
	void (*set_audio_sample)(retro_audio_sample_t);
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*set_input_poll)(retro_input_poll_t);
	void (*set_input_state)(retro_input_state_t);
	void (*init)(void);
	void (*deinit)(void);
	unsigned int (*api_version)(void);
	void (*get_system_info)(struct retro_system_info *);
	void (*get_system_av_info)(struct retro_system_av_info *);
	void (*set_controller_port_device)(unsigned int, unsigned int);
	void (*reset)(void);
	void (*run)(void);
	size_t (*serialize_size)(void);
	bool (*serialize)(void *, size_t);
	bool (*unserialize)(const void *, size_t);
	void (*cheat_reset)(void);
	void (*cheat_set)(unsigned int, bool, const char *);
	bool (*load_game)(const struct retro_game_info *);
	bool (*load_game_special)(unsigned int,
				  const struct retro_game_info *, size_t);
	void (*unload_game)(void);
	unsigned int (*get_region)(void);
	void *(*get_memory_data)(unsigned int);
	size_t (*get_memory_size)(unsigned int);
};

/* Fill the table (loading the core first if it is an llext).
 * Returns 0 on success.
 */
int retro_core_bind(struct retro_core_api *api);

/* Release the core (llext: teardown + unload + heap back to baseline;
 * static: no-op). The table is dead after this.
 */
void retro_core_unbind(void);

/* Choose which core the next bind loads (llext FS binder only; no-op
 * for static/embedded). The launcher menu sets this per selection.
 */
void retro_core_set_path(const char *path);

#endif /* PIZZA_RETRO_CORE_H */
