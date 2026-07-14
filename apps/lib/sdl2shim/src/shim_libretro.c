/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sdl2shim-over-libretro -- the generic glue that turns any sdl2shim
 * consumer into a libretro core (the retro_* ABI v1 exports).
 *
 * Control inversion: the game keeps its own main loop, running on a
 * dedicated thread; retro_run() is a two-semaphore rendezvous with it.
 * s2s_present_frame (reached from SDL_RenderPresent or an app video
 * TU) is the frame boundary: publish the pixels, wake retro_run, park
 * until the next one. RenderPresent's PRESENTVSYNC delay self-disables
 * because the park consumes the frame period, so its elapsed check
 * never fires.
 *
 * Input: retropad state is polled from the frontend each retro_run and
 * edge-converted into SDL KEYDOWN/KEYUP on the CORE's own shim event
 * queue (this llext contains its own shim_event.c) -- the game's input
 * path is unchanged, and the frontend's producers never reach in here.
 *
 * Teardown contract (the "wrapped apps aren't pure cores" risk): the
 * game thread only ever dies at the park point, where it holds no
 * locks -- never mid-malloc. retro_unload_game sets app_stop and wakes
 * the park; the thread self-aborts there; join reaps it. After that
 * the llext can be unloaded safely.
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>

#include "sdl2shim.h"
#include "sdl2shim_libretro.h"
#include <libretro.h>

extern int SDL_main(int argc, char *argv[]);

/* Core builds compile with -D__PICOLIBC_ERRNO_FUNCTION=<name>, which
 * flips picolibc's errno from a TLS variable (an import runtime llext
 * lookup can't resolve) to this function. One shared errno for the
 * whole core; the frontend's own errno domain is separate, and the
 * two never need to agree.
 */
#ifdef __PICOLIBC_ERRNO_FUNCTION
int *__PICOLIBC_ERRNO_FUNCTION(void)
{
	static int core_errno;

	return &core_errno;
}
#endif

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

/* EVERY kernel object is DONATED BY THE FRONTEND (exported symbols;
 * see retro_llext_exports.c). This is load-bearing ABI hygiene, not
 * style: struct k_thread/k_sem layouts depend on kernel config
 * (THREAD_LOCAL_STORAGE, THREAD_NAME, ...), and the core builds with
 * its own (minimal, TLS-free) config. A k_thread allocated HERE with
 * the core's sizeof gets initialized by the frontend's
 * k_thread_create with the frontend's LARGER layout -- overrunning
 * into whatever .bss neighbors it (M3 first-light: the rendezvous
 * semaphores, zeroed the moment the game thread was created). The
 * core therefore only ever passes POINTERS to frontend-defined
 * objects. Bonus for the stack: a K_THREAD_STACK_DEFINE here would
 * also add a second NOBITS section (loader rejects), and a stack
 * outside unloadable memory can never be the live stack of a thread
 * that outlives a buggy teardown.
 */
extern struct k_thread s2s_lr_app_thread;
extern struct k_sem s2s_lr_sem_run;
extern struct k_sem s2s_lr_sem_frame;
extern struct z_thread_stack_element s2s_lr_app_stack[];
extern const size_t s2s_lr_app_stack_size;

#define app_thread s2s_lr_app_thread
#define sem_run    s2s_lr_sem_run
#define sem_frame  s2s_lr_sem_frame

static const void *cur_pixels;
static int cur_pitch;
static volatile bool app_stop;
static bool app_started;
static bool has_bitmask;
static uint16_t pad_prev;

/* Content path handed to the wrapped app on its command line (M5). Empty
 * for a no_game core. Captured in retro_load_game, consumed by the game
 * thread's argv.
 */
static char content_path[256];

/* ── present seam (the glue IS the core's present backend) ────── */

int s2s_present_init(int width, int height)
{
	if (width != s2s_libretro_desc.width ||
	    height != s2s_libretro_desc.height) {
		printf("[s2s-lr] present %dx%d != desc %dx%d\n",
		       width, height,
		       s2s_libretro_desc.width, s2s_libretro_desc.height);
	}
	return 0;
}

void s2s_present_frame(const void *pixels, int pitch)
{
	cur_pixels = pixels;
	cur_pitch = pitch;
	k_sem_give(&sem_frame);
	k_sem_take(&sem_run, K_FOREVER);

	if (app_stop) {
		/* Die here, at the one point the game holds no locks.
		 * (Named thread object, not k_current_get(): that would
		 * pull in z_tls_current, a TLS import the core build
		 * deliberately avoids.)
		 */
		k_thread_abort(&app_thread);
	}
}

/* ── game thread ──────────────────────────────────────────────── */

static void app_thread_main(void *a, void *b, void *c)
{
	/* argv: "core" [content_arg] [content_path]. A no_game core runs
	 * with just "core"; a content core appends its flag (Doom: -iwad)
	 * and the picked file, so the game opens exactly that path.
	 */
	char *argv[4];
	int argc = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	argv[argc++] = (char *)"core";
	if (content_path[0] != '\0') {
		if (s2s_libretro_desc.content_arg != NULL) {
			argv[argc++] = (char *)s2s_libretro_desc.content_arg;
		}
		argv[argc++] = content_path;
	}
	argv[argc] = NULL;

	while (!app_stop) {
		SDL_main(argc, argv);
		/* Appliance semantics: the game quitting relaunches it
		 * (sokoban's ESC-to-title loop, Doom's demo cycle).
		 */
		if (!app_stop) {
			k_msleep(100);
		}
	}
}

/* ── input: retropad edges -> SDL key events on the core queue ── */

static void synth_key(uint16_t scancode, bool down)
{
	SDL_Event e;

	memset(&e, 0, sizeof(e));
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = (SDL_Scancode)scancode;
	s2s_event_submit(&e);
}

static void input_sync(void)
{
	uint16_t now = 0;

	if (!input_state_cb) {
		return;
	}
	if (input_poll_cb) {
		input_poll_cb();
	}

	if (has_bitmask) {
		now = (uint16_t)input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
					       RETRO_DEVICE_ID_JOYPAD_MASK);
	} else {
		for (unsigned int i = 0; i < 16; i++) {
			if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i)) {
				now |= (uint16_t)(1 << i);
			}
		}
	}

	uint16_t diff = now ^ pad_prev;

	if (diff) {
		for (size_t i = 0; i < s2s_libretro_desc.keymap_len; i++) {
			const struct s2s_libretro_keybind *kb =
				&s2s_libretro_desc.keymap[i];
			uint16_t bit = (uint16_t)(1 << kb->retro_id);

			if (diff & bit) {
				synth_key(kb->scancode, (now & bit) != 0);
			}
		}
	}
	pad_prev = now;
}

/* ── the retro_* ABI ──────────────────────────────────────────── */

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	bool no_game = !s2s_libretro_desc.needs_content;

	env_cb = cb;
	env_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
	has_bitmask = env_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
	audio_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	audio_batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
	input_state_cb = cb;
}

RETRO_API void retro_init(void)
{
	k_sem_init(&sem_run, 0, 1);
	k_sem_init(&sem_frame, 0, 1);
	cur_pixels = NULL;
	app_stop = false;
	app_started = false;
	pad_prev = 0;
}

RETRO_API void retro_deinit(void)
{
	retro_unload_game();
}

RETRO_API unsigned int retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = s2s_libretro_desc.name;
	info->library_version = s2s_libretro_desc.version;
	info->need_fullpath = s2s_libretro_desc.need_fullpath;
	info->valid_extensions = s2s_libretro_desc.valid_extensions;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	info->geometry.base_width = s2s_libretro_desc.width;
	info->geometry.base_height = s2s_libretro_desc.height;
	info->geometry.max_width = s2s_libretro_desc.width;
	info->geometry.max_height = s2s_libretro_desc.height;
	info->geometry.aspect_ratio = 0.0f;
	info->timing.fps = s2s_libretro_desc.fps;
	info->timing.sample_rate = s2s_libretro_desc.sample_rate;
}

RETRO_API void retro_set_controller_port_device(unsigned int port,
						unsigned int device)
{
	(void)port;
	(void)device;
}

RETRO_API void retro_reset(void)
{
}

RETRO_API void retro_run(void)
{
	input_sync();

	if (!app_started) {
		if (video_cb) {
			video_cb(NULL, s2s_libretro_desc.width,
				 s2s_libretro_desc.height, 0);
		}
		return;
	}

	k_sem_give(&sem_run);

	if (k_sem_take(&sem_frame, K_MSEC(250)) == 0 && cur_pixels) {
		video_cb(cur_pixels, s2s_libretro_desc.width,
			 s2s_libretro_desc.height, cur_pitch);
	} else {
		/* Game busy (level load etc.): dupe the frame. */
		video_cb(NULL, s2s_libretro_desc.width,
			 s2s_libretro_desc.height, 0);
	}

	/* Legacy path only: no-op while the producer thread runs. */
	if (audio_batch_cb) {
		s2s_audio_lr_pump(audio_batch_cb, s2s_libretro_desc.fps);
	}
}

RETRO_API bool retro_load_game(const struct retro_game_info *info)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

	/* Capture the picked content path for the game thread's argv. A
	 * no_game core gets info == NULL (or no path) -> empty. */
	if (info != NULL && info->path != NULL) {
		strncpy(content_path, info->path, sizeof(content_path) - 1);
		content_path[sizeof(content_path) - 1] = '\0';
	} else {
		content_path[0] = '\0';
	}

	if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		printf("[s2s-lr] frontend refused XRGB8888\n");
		return false;
	}

	k_thread_create(&app_thread, s2s_lr_app_stack,
			s2s_lr_app_stack_size,
			app_thread_main, NULL, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&app_thread, "lr_game");
	app_started = true;

	/* Audio-first production: the producer preempts the game, so a
	 * frame running over budget slows the GAME, never the audio.
	 */
	if (audio_batch_cb) {
		s2s_audio_lr_producer_start(audio_batch_cb);
	}
	return true;
}

RETRO_API bool retro_load_game_special(unsigned int type,
				       const struct retro_game_info *info,
				       size_t num)
{
	(void)type;
	(void)info;
	(void)num;
	return false;
}

RETRO_API void retro_unload_game(void)
{
	/* Producer first: its code lives in this llext, so it must be dead
	 * before the frontend can unload us.
	 */
	s2s_audio_lr_producer_stop();

	if (!app_started) {
		return;
	}
	app_stop = true;
	k_sem_give(&sem_run);
	if (k_thread_join(&app_thread, K_MSEC(1000)) != 0) {
		printf("[s2s-lr] game thread did not park -- aborting it\n");
		k_thread_abort(&app_thread);
	}
	app_started = false;
}

RETRO_API unsigned int retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

RETRO_API size_t retro_serialize_size(void)
{
	return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
	(void)data;
	(void)size;
	return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
	(void)data;
	(void)size;
	return false;
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned int index, bool enabled,
			       const char *code)
{
	(void)index;
	(void)enabled;
	(void)code;
}

RETRO_API void *retro_get_memory_data(unsigned int id)
{
	(void)id;
	return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned int id)
{
	(void)id;
	return 0;
}
