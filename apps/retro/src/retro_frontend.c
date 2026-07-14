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
#ifdef CONFIG_FILE_SYSTEM
#include <zephyr/fs/fs.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "sdl2shim.h"
#include <libretro.h>
#include "retro_frontend.h"
#include "retro_core.h"
#ifdef CONFIG_SDL2SHIM_AUDIO_HDMI
#include "retro_audio.h"
#endif

LOG_MODULE_REGISTER(retro, CONFIG_RETRO_LOG_LEVEL);

static struct retro_core_api core;
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static struct retro_frame_time_callback frame_time_cb;
static bool support_no_game;
static bool present_ready;

/* Split-lifecycle state (open -> run_loaded -> close). */
static struct retro_content_req content_req;
static bool core_open;    /* between open() and close() */
static bool game_loaded;  /* load_game() succeeded -> unload_game() owed */
static void *content_rom; /* RAM-loaded content, held until close() */

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

		switch (fmt) {
		case RETRO_PIXEL_FORMAT_0RGB1555:
		case RETRO_PIXEL_FORMAT_XRGB8888:
		case RETRO_PIXEL_FORMAT_RGB565:
			pixfmt = fmt;
			LOG_INF("env: pixel format %d", fmt);
			return true;
		default:
			LOG_ERR("env: pixel format %d unsupported", fmt);
			return false;
		}
	}

	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		support_no_game = *(const bool *)data;
		return true;

#ifdef CONFIG_RETRO_STORAGE_MOUNT
	/* Content-consuming cores read these in init and some NULL-deref on
	 * a false return. Fixed dirs on the frontend's volume (created at
	 * mount); the returned string must outlive the call, so a literal.
	 */
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char **)data = CONFIG_RETRO_STORAGE_MOUNT "/system";
		return true;

	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = CONFIG_RETRO_STORAGE_MOUNT "/saves";
		return true;

	case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
		*(const char **)data = CONFIG_RETRO_STORAGE_MOUNT "/system";
		return true;
#endif

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

/* The present seam (s2s_present_frame) and every other caller of it (menu,
 * sdl2shim-over-libretro) speak 32bpp ARGB. A core, though, emits whichever
 * format it selected via SET_PIXEL_FORMAT: XRGB8888 passes straight through
 * (zero-copy, the 2048/sokoban path), while RGB565 and 0RGB1555 are expanded
 * into a frontend-owned ARGB scratch first. Keeping this in the frontend --
 * the one place that knows libretro pixel formats -- leaves the shared seam's
 * contract untouched.
 */
static uint32_t *present_scratch;
static size_t present_scratch_px;

static uint32_t *scratch_for(unsigned int w, unsigned int h)
{
	size_t need = (size_t)w * h;

	if (need > present_scratch_px) {
		uint32_t *p = realloc(present_scratch, need * 4);

		if (!p) {
			LOG_ERR("present: scratch alloc (%ux%u) failed", w, h);
			return NULL;
		}
		present_scratch = p;
		present_scratch_px = need;
	}
	return present_scratch;
}

static void present_core_frame(const void *data, unsigned int w,
			       unsigned int h, size_t pitch)
{
	if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
		s2s_present_frame(data, (int)pitch);
		return;
	}

	uint32_t *dst = scratch_for(w, h);

	if (!dst) {
		return;
	}

	const uint8_t *src_row = data;

	for (unsigned int y = 0; y < h; y++) {
		const uint16_t *px = (const uint16_t *)src_row;
		uint32_t *o = dst + (size_t)y * w;

		if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
			for (unsigned int x = 0; x < w; x++) {
				uint32_t p = px[x];
				uint32_t r = (p >> 11) & 0x1F;
				uint32_t g = (p >> 5) & 0x3F;
				uint32_t b = p & 0x1F;

				o[x] = 0xFF000000u |
				       (((r << 3) | (r >> 2)) << 16) |
				       (((g << 2) | (g >> 4)) << 8) |
				       ((b << 3) | (b >> 2));
			}
		} else { /* RETRO_PIXEL_FORMAT_0RGB1555 */
			for (unsigned int x = 0; x < w; x++) {
				uint32_t p = px[x];
				uint32_t r = (p >> 10) & 0x1F;
				uint32_t g = (p >> 5) & 0x1F;
				uint32_t b = p & 0x1F;

				o[x] = 0xFF000000u |
				       (((r << 3) | (r >> 2)) << 16) |
				       (((g << 3) | (g >> 2)) << 8) |
				       ((b << 3) | (b >> 2));
			}
		}
		src_row += pitch;
	}
	s2s_present_frame(dst, (int)(w * 4));
}

static void video_cb(const void *data, unsigned int width,
		     unsigned int height, size_t pitch)
{
	if (!data || !present_ready) {
		return; /* NULL = frame dupe */
	}
	present_core_frame(data, width, height, pitch);
}

static void audio_sample_cb(int16_t left, int16_t right)
{
	(void)left;
	(void)right;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
#ifdef CONFIG_SDL2SHIM_AUDIO_HDMI
	/* All-or-nothing accept; 0 = ring full, the glue producer's
	 * backpressure signal (it retries the same chunk).
	 */
	return retro_audio_push(data, frames);
#else
	(void)data; /* no audio backend (qemu): consume and drop */
	return frames;
#endif
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

#ifdef CONFIG_RETRO_MENU
static volatile bool menu_flag;

void retro_request_menu(void)
{
	menu_flag = true;
}

/* Return to the launcher on the pad's Home button (a single button,
 * mapped to ESCAPE in retro_btinput.c -- reliable, unlike a two-button
 * chord the tiny 8BitDo may not report together), the L+R chord, or a
 * `retro menu` shell request. Reads the persistent keyboard-state
 * array, so it never competes with the core's own event draining.
 */
static bool menu_requested(void)
{
	const Uint8 *ks = SDL_GetKeyboardState(NULL);

	if (menu_flag) {
		menu_flag = false;
		return true;
	}
	return ks[SDL_SCANCODE_ESCAPE] ||
	       (ks[SDL_SCANCODE_Q] && ks[SDL_SCANCODE_W]);
}
#else
static bool menu_requested(void)
{
	return false;
}
#endif

#ifdef CONFIG_FILE_SYSTEM
/* Slurp a content file into a malloc'd buffer (need_fullpath == false
 * cores take the bytes directly). Caller owns the buffer; the frontend
 * holds it in content_rom until close() -- the libretro contract lets the
 * frontend free after load_game(), but keeping it alive covers the cores
 * that (against spec) retain the pointer, and GB/NES-class ROMs are small.
 */
static void *read_file(const char *path, size_t *out_sz)
{
	struct fs_dirent st;
	struct fs_file_t f;
	void *buf;
	ssize_t rd;

	if (fs_stat(path, &st) != 0 || st.type != FS_DIR_ENTRY_FILE) {
		printf("[retro] content stat failed: %s\n", path);
		return NULL;
	}

	buf = malloc(st.size);
	if (!buf) {
		printf("[retro] content alloc %zu failed\n", (size_t)st.size);
		return NULL;
	}

	fs_file_t_init(&f);
	if (fs_open(&f, path, FS_O_READ) != 0) {
		printf("[retro] content open failed: %s\n", path);
		free(buf);
		return NULL;
	}

	rd = fs_read(&f, buf, st.size);
	fs_close(&f);

	if (rd != (ssize_t)st.size) {
		printf("[retro] content short read (%zd/%zu)\n",
		       rd, (size_t)st.size);
		free(buf);
		return NULL;
	}

	*out_sz = (size_t)st.size;
	return buf;
}
#endif /* CONFIG_FILE_SYSTEM */

int retro_frontend_open(void)
{
	struct retro_system_info si;
	int rc;

	/* Fresh per-bind state (the frontend relaunches across core
	 * load/unload cycles).
	 */
	pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
	memset(&frame_time_cb, 0, sizeof(frame_time_cb));
	support_no_game = false;
	present_ready = false;
	game_loaded = false;
	content_rom = NULL;
	memset(&content_req, 0, sizeof(content_req));

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

	/* si.library_name / si.valid_extensions are pointers into the core's
	 * rodata, valid until unbind -- safe to hold in content_req until
	 * close().
	 */
	content_req.wants_content = !support_no_game;
	content_req.need_fullpath = si.need_fullpath;
	content_req.valid_extensions = si.valid_extensions;
	content_req.library_name = si.library_name;

	core_open = true;
	return 0;
}

const struct retro_content_req *retro_frontend_content_req(void)
{
	return &content_req;
}

int retro_frontend_run_loaded(const char *content_path)
{
	struct retro_system_av_info av;
	struct k_timer frame_timer;
	struct retro_game_info info;
	const struct retro_game_info *infop = NULL;

	memset(&info, 0, sizeof(info)); /* also silences unused-var when NULL */

	if (content_path && content_path[0]) {
#ifdef CONFIG_FILE_SYSTEM
		info.path = content_path;

		if (!content_req.need_fullpath) {
			size_t sz = 0;

			content_rom = read_file(content_path, &sz);
			if (!content_rom) {
				return -1;
			}
			info.data = content_rom;
			info.size = sz;
		}
		infop = &info;
		printf("[retro] content: %s (%s, %zu bytes)\n", content_path,
		       content_req.need_fullpath ? "path" : "data", info.size);
#else
		printf("[retro] content requested but no filesystem\n");
		return -1;
#endif
	}

	if (!core.load_game(infop)) {
		printf("[retro] retro_load_game(%s) failed\n",
		       infop ? "content" : "NULL");
		return -1;
	}
	game_loaded = true;

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
		if (menu_requested()) {
			printf("[retro] return-to-menu requested\n");
			break;
		}

		/* Plain frame-timer pacing. Audio no longer rides this loop:
		 * the glue's producer thread mixes above the game's priority
		 * and paces itself off the frontend ring's backpressure (the
		 * MAI drain rate), so the audio clock is the DMA, not this
		 * timer.
		 */
		k_timer_status_sync(&frame_timer);
	}

	k_timer_stop(&frame_timer);
	printf("[retro] core ran %u frames\n", frames_run);
	return 0;
}

void retro_frontend_close(void)
{
	if (!core_open) {
		return;
	}

	if (game_loaded) {
		core.unload_game();
		game_loaded = false;
	}
	core.deinit();
	retro_core_unbind();

	if (content_rom) {
		free(content_rom);
		content_rom = NULL;
	}
	core_open = false;
	log_llext_heap("post-unbind");
}

int retro_frontend_run(void)
{
	int rc = retro_frontend_open();

	if (rc != 0) {
		return rc;
	}

	rc = retro_frontend_run_loaded(NULL);
	retro_frontend_close();
	return rc;
}
