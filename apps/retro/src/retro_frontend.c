/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- callback adapter + run loop.
 *
 * Frontend half of the libretro contract (ABI v1): supplies the 6
 * callbacks, answers the environment commands a software-rendered core
 * needs, and paces retro_run() at av_info.timing.fps. Video lands on
 * the s2s_present_* seam; input comes from the shim event queue's
 * keyboard-state array, so every existing producer (scripted timeline,
 * CDC shell, btinput) drives the joypad unchanged.
 *
 * The core is statically linked at M0 (the RetroArch console
 * fused-binary model); M2 replaces the direct retro_* calls with an
 * llext-resolved dispatch table.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "sdl2shim.h"
#include <libretro.h>
#include "retro_frontend.h"
#include "retro_core.h"

LOG_MODULE_REGISTER(retro, CONFIG_RETRO_LOG_LEVEL);

static struct retro_core_api core;
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static struct retro_frame_time_callback frame_time_cb;
static bool support_no_game;
static bool present_ready;

/* llext heap watermark (cycle-test evidence: allocated must return to
 * baseline after every unbind).
 */
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS) && defined(CONFIG_LLEXT)
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>

extern struct k_heap llext_heap;

static void log_llext_heap(const char *tag)
{
	struct sys_memory_stats st;

	if (sys_heap_runtime_stats_get(&llext_heap.heap, &st) == 0) {
		printf("[retro] llext heap %s: alloc %zu free %zu max %zu\n",
		       tag, st.allocated_bytes, st.free_bytes,
		       st.max_allocated_bytes);
	}
}
#else
static void log_llext_heap(const char *tag)
{
	(void)tag;
}
#endif

/* RETRO_DEVICE_ID_JOYPAD_* (0..15) -> SDL scancode in the shim's
 * keyboard-state array. Arrows/START/SELECT are what 2048 uses; the
 * letter rows follow the common desktop-frontend defaults so later
 * cores inherit something sane.
 */
static const SDL_Scancode joypad_map[16] = {
	[RETRO_DEVICE_ID_JOYPAD_B]      = SDL_SCANCODE_Z,
	[RETRO_DEVICE_ID_JOYPAD_Y]      = SDL_SCANCODE_A,
	[RETRO_DEVICE_ID_JOYPAD_SELECT] = SDL_SCANCODE_TAB,
	[RETRO_DEVICE_ID_JOYPAD_START]  = SDL_SCANCODE_RETURN,
	[RETRO_DEVICE_ID_JOYPAD_UP]     = SDL_SCANCODE_UP,
	[RETRO_DEVICE_ID_JOYPAD_DOWN]   = SDL_SCANCODE_DOWN,
	[RETRO_DEVICE_ID_JOYPAD_LEFT]   = SDL_SCANCODE_LEFT,
	[RETRO_DEVICE_ID_JOYPAD_RIGHT]  = SDL_SCANCODE_RIGHT,
	[RETRO_DEVICE_ID_JOYPAD_A]      = SDL_SCANCODE_X,
	[RETRO_DEVICE_ID_JOYPAD_X]      = SDL_SCANCODE_S,
	[RETRO_DEVICE_ID_JOYPAD_L]      = SDL_SCANCODE_Q,
	[RETRO_DEVICE_ID_JOYPAD_R]      = SDL_SCANCODE_W,
	[RETRO_DEVICE_ID_JOYPAD_L2]     = SDL_SCANCODE_E,
	[RETRO_DEVICE_ID_JOYPAD_R2]     = SDL_SCANCODE_R,
	[RETRO_DEVICE_ID_JOYPAD_L3]     = SDL_SCANCODE_T,
	[RETRO_DEVICE_ID_JOYPAD_R3]     = SDL_SCANCODE_Y,
};

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	printf("[core:%u] %s", (unsigned int)level, msg);
}

static bool env_cb(unsigned int cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;

	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		enum retro_pixel_format fmt =
			*(const enum retro_pixel_format *)data;

		if (fmt != RETRO_PIXEL_FORMAT_XRGB8888) {
			LOG_ERR("env: pixel format %d unsupported "
				"(XRGB8888 only)", fmt);
			return false;
		}
		pixfmt = fmt;
		return true;
	}

	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		support_no_game = *(const bool *)data;
		return true;

	case RETRO_ENVIRONMENT_SET_VARIABLES:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		return true;

	case RETRO_ENVIRONMENT_GET_VARIABLE:
		/* No stored options: core keeps its defaults. */
		return false;

	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;

	case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
		frame_time_cb =
			*(const struct retro_frame_time_callback *)data;
		return true;

	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;

	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;

	default:
		LOG_DBG("env: unhandled cmd %u%s",
			cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL,
			(cmd & RETRO_ENVIRONMENT_EXPERIMENTAL) ? " (exp)" : "");
		return false;
	}
}

static void video_cb(const void *data, unsigned int width,
		     unsigned int height, size_t pitch)
{
	if (!data || !present_ready) {
		return; /* NULL = frame dupe */
	}
	s2s_present_frame(data, (int)pitch);
}

static void audio_sample_cb(int16_t left, int16_t right)
{
	(void)left;
	(void)right;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
	(void)data;
	return frames; /* consumed (discarded) -- audio ring lands at M1 */
}

static void input_poll_cb(void)
{
	SDL_Event e;

	/* Keyboard state updates at submit time; drain the queue so it
	 * never fills. (The menu will consume these properly at M4.)
	 */
	while (SDL_PollEvent(&e)) {
	}
}

static int16_t input_state_cb(unsigned int port, unsigned int device,
			      unsigned int index, unsigned int id)
{
	const Uint8 *ks;

	if (port != 0 || device != RETRO_DEVICE_JOYPAD) {
		return 0;
	}

	ks = SDL_GetKeyboardState(NULL);
	(void)index;

	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
		int16_t mask = 0;

		for (unsigned int i = 0; i < 16; i++) {
			if (joypad_map[i] && ks[joypad_map[i]]) {
				mask |= (int16_t)(1 << i);
			}
		}
		return mask;
	}

	if (id < 16 && joypad_map[id]) {
		return ks[joypad_map[id]] ? 1 : 0;
	}
	return 0;
}

int retro_frontend_run(void)
{
	struct retro_system_info si;
	struct retro_system_av_info av;
	struct k_timer frame_timer;
	int rc;

	/* Fresh per-bind state (the frontend relaunches across core
	 * load/unload cycles).
	 */
	pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
	memset(&frame_time_cb, 0, sizeof(frame_time_cb));
	support_no_game = false;
	present_ready = false;

	log_llext_heap("pre-bind");
	rc = retro_core_bind(&core);
	if (rc != 0) {
		printf("[retro] core bind failed (%d)\n", rc);
		return rc;
	}

	core.set_environment(env_cb);
	core.set_video_refresh(video_cb);
	core.set_audio_sample(audio_sample_cb);
	core.set_audio_sample_batch(audio_batch_cb);
	core.set_input_poll(input_poll_cb);
	core.set_input_state(input_state_cb);

	core.init();

	memset(&si, 0, sizeof(si));
	core.get_system_info(&si);
	printf("[retro] core: %s %s (no_game=%d, api %u)\n",
	       si.library_name ? si.library_name : "?",
	       si.library_version ? si.library_version : "?",
	       (int)support_no_game, core.api_version());

	if (!core.load_game(NULL)) {
		printf("[retro] retro_load_game(NULL) failed\n");
		core.deinit();
		retro_core_unbind();
		return -1;
	}

	core.get_system_av_info(&av);
	printf("[retro] geometry %ux%u max %ux%u fps %d sample_rate %d\n",
	       av.geometry.base_width, av.geometry.base_height,
	       av.geometry.max_width, av.geometry.max_height,
	       (int)av.timing.fps, (int)av.timing.sample_rate);

	if (s2s_present_init((int)av.geometry.base_width,
			     (int)av.geometry.base_height) == 0) {
		present_ready = true;
	} else {
		printf("[retro] present init failed -- running headless\n");
	}

	double fps = (av.timing.fps > 1.0) ? av.timing.fps : 60.0;
	uint32_t period_us = (uint32_t)(1000000.0 / fps);

	k_timer_init(&frame_timer, NULL, NULL);
	k_timer_start(&frame_timer, K_USEC(period_us), K_USEC(period_us));

	uint64_t last_us = k_ticks_to_us_floor64(k_uptime_ticks());
	uint32_t frames_run = 0;

	for (;;) {
		uint64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks());

		if (frame_time_cb.callback) {
			frame_time_cb.callback(
				(retro_usec_t)(now_us - last_us));
		}
		last_us = now_us;

		core.run();
		frames_run++;

		if (CONFIG_RETRO_LLEXT_CYCLE_TEST > 0 &&
		    frames_run >= CONFIG_RETRO_LLEXT_CYCLE_TEST) {
			break;
		}

		k_timer_status_sync(&frame_timer);
	}

	/* Cycle-test path: full teardown, then the caller relaunches
	 * and the next bind reloads the core from scratch.
	 */
	k_timer_stop(&frame_timer);
	printf("[retro] cycle: ran %u frames, unloading core\n", frames_run);
	core.unload_game();
	core.deinit();
	retro_core_unbind();
	log_llext_heap("post-unbind");
	return 0;
}
