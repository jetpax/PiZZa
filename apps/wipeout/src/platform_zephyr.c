/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PiZZa WipEout port -- platform.h backend + Zephyr boot wiring.
 *
 * Architecture (locked):
 *   - RENDERER=SOFTWARE (pure C); engine renders into a 320x240
 *     rgba_t backbuffer in cached RAM.
 *   - HVS hardware scaling: vc_fb DT exposes render-width=320,
 *     render-height=240. VC upscales every frame to the monitor's
 *     EDID-negotiated native mode at zero ARM cost.
 *   - Present: byte-swap-in-place (engine rgba_t bytes are R,G,B,A;
 *     VC ARGB_8888 expects B,G,R,A in memory) then async DMA blit.
 *   - SD card auto-mounted at /SD: (FATFS); assets at /SD:/wipeout/.
 *   - Audio + BT HID out of scope this milestone.
 *
 * The Zephyr boot model is platform-calls-you: main() does board
 * bring-up, then either runs the game loop on the main thread or
 * (here) spawns a dedicated render thread. The shell's USB CDC
 * service keeps running on the system workqueue; the render thread
 * yields once per frame so the shell stays responsive.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/bcm2835_fb.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rpi_fw.h>

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)
#include <zephyr/usb/usbd.h>
#include <sample_usbd.h>
#endif

/* Engine headers. types.h pulls in stdint/math; platform.h declares
 * the 12-function contract this file implements; render.h gives us
 * vec2i_t, render_resolution_t, render_set_resolution, etc.; system.h
 * the system_init/system_update entry points.
 */
#include "types.h"
#include "platform.h"
#include "render.h"
#include "input.h"
#include "system.h"

LOG_MODULE_REGISTER(pizza_wipeout, LOG_LEVEL_INF);

/* Renderer parallel-flush knob (defined in render_software_smp.c).
 * Not in render.h -- it's a port-private switch the M5 benchmark
 * shell command toggles.
 */
extern void render_software_set_parallel(int enabled, int bands, int threshold);
extern int  render_software_get_parallel_bands(void);

/* ------------------------------------------------------------------ *
 *  Display state
 * ------------------------------------------------------------------ */

#define PIZZA_W 320
#define PIZZA_H 240
#define PIZZA_PX (PIZZA_W * PIZZA_H)

/* 64-byte aligned: DMA fast path + cache-line clean range. Size is
 * exactly 4800 cache lines so sys_cache_data_flush_range covers
 * nothing outside this buffer.
 */
static rgba_t pizza_backbuf[PIZZA_PX] __aligned(64);

static const struct device *pizza_display;
static int pizza_display_ready;

/* ------------------------------------------------------------------ *
 *  platform.h timing / window backend
 * ------------------------------------------------------------------ */

double platform_now(void)
{
	return (double)k_uptime_get() / 1000.0;
}

vec2i_t platform_screen_size(void)
{
	return (vec2i_t){PIZZA_W, PIZZA_H};
}

bool platform_get_fullscreen(void) { return true; }
void platform_set_fullscreen(bool on) { (void)on; }

void platform_exit(void)
{
	printk("[wipeout] platform_exit -- halting\n");
	k_fatal_halt(0);
}

/* ------------------------------------------------------------------ *
 *  platform.h audio backend (silent stub)
 * ------------------------------------------------------------------ */

static void (*pizza_audio_cb)(float *, uint32_t);

void platform_set_audio_mix_cb(void (*cb)(float *, uint32_t))
{
	pizza_audio_cb = cb;
	/* TODO(board): drive cb from an I2S DMA buffer-complete handler. */
}

/* ------------------------------------------------------------------ *
 *  platform.h screenbuffer backend (software renderer)
 *
 *  render_software calls this once per render_frame_prepare() to
 *  grab the destination pointer and byte-pitch. We hand back our
 *  cached rgba_t backbuffer; bytes are R,G,B,A in memory. The engine
 *  paints into it serial or banded (see render_software_smp.c).
 * ------------------------------------------------------------------ */

rgba_t *platform_get_screenbuffer(int32_t *pitch_bytes)
{
	*pitch_bytes = (int32_t)(PIZZA_W * sizeof(rgba_t));
	return pizza_backbuf;
}

/* ------------------------------------------------------------------ *
 *  platform.h asset backend
 *
 *  platform_open_asset returns a stdio FILE*; this firmware does NOT
 *  wire fopen to FATFS (no POSIX fs shim), so we return NULL. The
 *  only direct callers are sfx music streaming (graceful: empty
 *  music) and intro.mpeg (graceful: skips straight to TITLE).
 *
 *  platform_load_asset / platform_load_userdata route through the
 *  engine's file_load helper, which is --wrap'd to our fs_*-backed
 *  __wrap_file_load in wipeout_glue.c.
 * ------------------------------------------------------------------ */

#define PATH_PREFIX_ASSETS  ""              /* engine asks for "wipeout/foo" already */
#define PATH_PREFIX_USERDATA "userdata/"    /* save.dat etc. */

FILE *platform_open_asset(const char *name, const char *mode)
{
	(void)name; (void)mode;
	/* Streaming I/O not supported in this image. Callers that need
	 * a FILE* (sfx_music_open, intro.mpeg) handle NULL gracefully.
	 */
	return NULL;
}

uint8_t *platform_load_asset(const char *name, uint32_t *len)
{
	extern uint8_t *file_load(const char *path, uint32_t *bytes_read);
	return file_load(name, len);
}

uint8_t *platform_load_userdata(const char *name, uint32_t *len)
{
	extern bool file_exists(const char *path);
	extern uint8_t *file_load(const char *path, uint32_t *bytes_read);
	char path[128];

	snprintf(path, sizeof(path), "%s%s", PATH_PREFIX_USERDATA, name);
	if (!file_exists(path)) {
		*len = 0;
		return NULL;
	}
	return file_load(path, len);
}

uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len)
{
	extern uint32_t file_store(const char *path, void *bytes, int32_t len);
	char path[128];

	snprintf(path, sizeof(path), "%s%s", PATH_PREFIX_USERDATA, name);
	return file_store(path, bytes, len);
}

/* ------------------------------------------------------------------ *
 *  USB CDC bring-up (boards with the USB device stack)
 * ------------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)
static int pizza_usb_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(NULL);

	if (usbd == NULL) {
		printk("[wipeout] sample_usbd_init_device failed\n");
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(usbd)) {
		int err = usbd_enable(usbd);
		if (err) {
			printk("[wipeout] usbd_enable failed: %d\n", err);
			return err;
		}
	}
	return 0;
}
/*
 * Bring CDC up at APPLICATION init, not from main(). The display + FATFS
 * (microSD) + SMP bring-up that runs during main() add enough latency
 * to push usbd_enable past macOS's USB enumeration retry window when
 * the mini-UART is connected at boot, which silently kills CDC on the
 * host side until the next replug. Running at APPLICATION captures
 * the enumeration window early; the rest of main() then runs while
 * the host completes descriptor exchange in parallel.
 */
SYS_INIT(pizza_usb_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#else
static inline int pizza_usb_start(void) { return 0; }
#endif

/* ------------------------------------------------------------------ *
 *  Present: byte-swap-in-place + async DMA blit
 *
 *  Engine rgba_t bytes (R,G,B,A) don't match the VC ARGB_8888 layout
 *  (memory B,G,R,A). Swap byte[0] <-> byte[2] in the backbuffer; the
 *  next render_frame_prepare() clears the whole buffer, so the
 *  "wrong-order" state only persists from end-of-frame N until the
 *  start of frame N+1, never read by the renderer.
 *
 *  Pixel-loop walks rgba_t* (a 4-byte struct); on aarch64 with the
 *  buffer __aligned(64) the compiler vectorises this into LDR W /
 *  REV W / STR W pairs at ~one cycle per pixel.
 *
 *  Returns the wait+blit cycles for fps accounting.
 * ------------------------------------------------------------------ */

static void pizza_byteswap_inplace(rgba_t *buf, uint32_t pixels)
{
	uint8_t *b = (uint8_t *)buf;
	for (uint32_t i = 0; i < pixels; i++) {
		uint8_t r = b[0];
		b[0] = b[2];
		b[2] = r;
		b += 4;
	}
}

static int pizza_present(void)
{
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(pizza_backbuf),
		.width    = PIZZA_W,
		.height   = PIZZA_H,
		.pitch    = PIZZA_W,
		.frame_incomplete = false,
	};

	pizza_byteswap_inplace(pizza_backbuf, PIZZA_PX);

	/* display_write is synchronous: DMA fast path engages when
	 * width==virt_width and pitch==width (the case here -- 320 x 4 =
	 * 1280 is 64-byte aligned and VC returns no padding); the driver
	 * flushes the source cache range internally before kicking.
	 *
	 * We use the sync API instead of bcm2835_fb_write_async because
	 * we have a single backbuffer: an async kick from frame N would
	 * still be reading pizza_backbuf when frame N+1's
	 * render_frame_prepare() starts clearing it. Adding a second
	 * backbuffer would lift this restriction and let frame time
	 * approach max(render, blit) -- a clean future optimisation but
	 * not load-bearing for M4 (render dwarfs blit at 240p).
	 */
	return display_write(pizza_display, 0, 0, &desc, pizza_backbuf);
}

/* ------------------------------------------------------------------ *
 *  Game loop -- runs on a dedicated thread, owns the present step
 *
 *  system_update() internally calls render_frame_prepare ->
 *  game_update -> render_frame_end -> render_flush, leaving the
 *  finished frame in pizza_backbuf. We present afterwards.
 *
 *  fps and per-phase cycle counts are tracked here; the `wipeout`
 *  shell commands read these.
 * ------------------------------------------------------------------ */

#define GAME_STACK_SIZE 32768

K_THREAD_STACK_DEFINE(pizza_game_stack, GAME_STACK_SIZE);
static struct k_thread pizza_game_thread;

static volatile uint32_t pizza_fps_x100;   /* fps * 100, sampled every 1s */
static volatile uint32_t pizza_us_update;  /* avg per-frame system_update us */
static volatile uint32_t pizza_us_present; /* avg per-frame present us */
static volatile uint32_t pizza_frames_total;
static volatile uint32_t pizza_last_tris;

/* Flag set by shell `wipeout capture` to ask the loop to dump the
 * next finished frame to /SD:/wipeout-frame.ppm. Cleared after dump.
 */
static volatile int pizza_capture_requested;

extern int pizza_capture_frame(const rgba_t *buf, int w, int h);

extern void pizza_sd_diag(void);

/* ------------------------------------------------------------------ *
 *  Deterministic synthetic-scene benchmark (`wipeout bench`)
 *
 *  Runs IN the game thread, replacing system_update() for N frames
 *  with a hand-built render call sequence that has bit-identical
 *  geometry every time (RNG reseeded per frame, fixed mvps, fixed
 *  primitive shape). Lets us compare scalar-vs-QPU builds at the
 *  same workload without trying to navigate the game to a matched
 *  scene.
 * ------------------------------------------------------------------ */

static volatile bool     pizza_bench_active;
static volatile int      pizza_bench_remaining;
static volatile int      pizza_bench_n_tris;
static volatile int      pizza_bench_n_mvps;
static volatile uint32_t pizza_bench_min_cyc;
static volatile uint32_t pizza_bench_max_cyc;
static volatile uint64_t pizza_bench_total_cyc;
static volatile uint32_t pizza_bench_frames_done;
static volatile uint32_t pizza_bench_stat_frames;
static volatile bool     pizza_bench_done;

static mat4_t pizza_bench_mvps[16];

#define PIZZA_BENCH_WARMUP_FRAMES 2

static void pizza_bench_one_frame(int n_tris, int n_mvps)
{
	/* Fixed RNG seed reset every frame -> identical geometry every
	 * frame -> stable per-frame timing across runs and across builds.
	 */
	uint32_t rng = 0xCAFEBABEu;

	render_frame_prepare();

	/* Pin view to identity-at-origin so the engine's projection_mat
	 * (set up by render_set_screen_size during render_init) combined
	 * with our bench mvps puts everything in front of the camera
	 * deterministically.
	 */
	render_set_view(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 0.0f));

	int per_mvp = (n_tris + n_mvps - 1) / n_mvps;
	int cur_mvp = -1;

	for (int t = 0; t < n_tris; t++) {
		int mvp_idx = (t / per_mvp);
		if (mvp_idx >= n_mvps) {
			mvp_idx = n_mvps - 1;
		}
		if (mvp_idx != cur_mvp) {
			render_set_model_mat(&pizza_bench_mvps[mvp_idx]);
			cur_mvp = mvp_idx;
		}

		rng = rng * 1664525u + 1013904223u;
		float x = ((float)(int32_t)rng / 2147483648.0f) * 40.0f;
		rng = rng * 1664525u + 1013904223u;
		float y = ((float)(int32_t)rng / 2147483648.0f) * 30.0f;
		rng = rng * 1664525u + 1013904223u;
		/* Z in [-100, -50] -- always in front of the camera so
		 * clip_near doesn't reject and reshape per-frame work.
		 */
		float z = -50.0f - (((float)(uint32_t)rng / 4294967296.0f) * 50.0f);

		tris_t tri = {{
			{.pos = vec3(x,        y,        z), .uv = vec2(0.0f,  0.0f),  .color = rgba(255, 100, 100, 255)},
			{.pos = vec3(x + 1.5f, y,        z), .uv = vec2(64.0f, 0.0f),  .color = rgba(100, 255, 100, 255)},
			{.pos = vec3(x,        y + 1.5f, z), .uv = vec2(0.0f,  64.0f), .color = rgba(100, 100, 255, 255)},
		}};
		render_push_tris(tri, RENDER_NO_TEXTURE);
	}

	render_frame_end();
}

static void pizza_game_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	printk("[wipeout] game thread up; SD diagnostic:\n");
	pizza_sd_diag();
	printk("[wipeout] calling system_init\n");
	system_init();
	printk("[wipeout] system_init OK; entering render loop\n");

	int64_t fps_window_start = k_uptime_get();
	uint32_t frames_in_window = 0;
	uint64_t cyc_update = 0;
	uint64_t cyc_present = 0;
	const uint64_t cycles_per_sec = sys_clock_hw_cycles_per_sec();

	for (;;) {
		bool benching = pizza_bench_active;
		uint32_t c0 = k_cycle_get_32();

		if (benching) {
			pizza_bench_one_frame(pizza_bench_n_tris, pizza_bench_n_mvps);
		} else {
			system_update();
		}
		uint32_t c1 = k_cycle_get_32();

		if (benching) {
			uint32_t cyc = c1 - c0;

			pizza_bench_frames_done++;
			/* Skip warmup: first few frames pay cold-cache + first-
			 * kick QPU allocation costs that don't reflect steady
			 * state. Discard from stats but still tick remaining.
			 */
			if (pizza_bench_frames_done > PIZZA_BENCH_WARMUP_FRAMES) {
				pizza_bench_total_cyc += cyc;
				if (cyc < pizza_bench_min_cyc) {
					pizza_bench_min_cyc = cyc;
				}
				if (cyc > pizza_bench_max_cyc) {
					pizza_bench_max_cyc = cyc;
				}
				pizza_bench_stat_frames++;
			}
			if (--pizza_bench_remaining <= 0) {
				pizza_bench_active = false;
				pizza_bench_done = true;
			}
		}

		(void)pizza_present();
		uint32_t c2 = k_cycle_get_32();

		/* Capture happens BEFORE the next render_frame_prepare()
		 * clears the buffer. Note pizza_backbuf is currently
		 * byte-swapped (B,G,R,A); the capture helper inverts.
		 */
		if (pizza_capture_requested) {
			pizza_capture_requested = 0;
			pizza_capture_frame(pizza_backbuf, PIZZA_W, PIZZA_H);
		}

		cyc_update  += (uint32_t)(c1 - c0);
		cyc_present += (uint32_t)(c2 - c1);
		frames_in_window++;
		pizza_frames_total++;

		const render_stats_t *st = render_frame_get_stats();
		if (st) {
			pizza_last_tris = st->num_tris;
		}

		const int64_t now = k_uptime_get();
		const int64_t elapsed_ms = now - fps_window_start;
		if (elapsed_ms >= 1000) {
			pizza_fps_x100 =
				(uint32_t)((int64_t)frames_in_window * 100000 / elapsed_ms);
			pizza_us_update =
				(uint32_t)((cyc_update * 1000000U) /
					   (cycles_per_sec * frames_in_window));
			pizza_us_present =
				(uint32_t)((cyc_present * 1000000U) /
					   (cycles_per_sec * frames_in_window));
			printk("[wipeout] %u.%02u fps  update=%u us  present=%u us  tris=%u  bands=%d\n",
			       pizza_fps_x100 / 100, pizza_fps_x100 % 100,
			       pizza_us_update, pizza_us_present,
			       pizza_last_tris,
			       render_software_get_parallel_bands());
			fps_window_start = now;
			frames_in_window = 0;
			cyc_update = cyc_present = 0;
		}

		/* Cooperate with the shell + USBD + log thread. */
		k_yield();
	}
}

/* ------------------------------------------------------------------ *
 *  Shell command set: `wipeout <sub>`
 * ------------------------------------------------------------------ */

#define VC_CLOCK_ID_ARM  3U

static int cmd_wipeout_fps(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_print(sh, "fps=%u.%02u  update=%u us  present=%u us",
		    pizza_fps_x100 / 100, pizza_fps_x100 % 100,
		    pizza_us_update, pizza_us_present);
	shell_print(sh, "tris/frame=%u  bands=%d  frames_total=%u",
		    pizza_last_tris,
		    render_software_get_parallel_bands(),
		    pizza_frames_total);
	return 0;
}

static int cmd_wipeout_clock(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	const struct device *fw = DEVICE_DT_GET_ONE(raspberrypi_bcm283x_firmware);
	uint32_t cur[2] = {VC_CLOCK_ID_ARM, 0};
	uint32_t max[2] = {VC_CLOCK_ID_ARM, 0};

	if (!device_is_ready(fw)) {
		shell_error(sh, "rpi_fw device not ready");
		return -ENODEV;
	}
	int e1 = rpi_fw_transfer(fw, RPI_FW_TAG_GET_CLOCK_RATE, cur, sizeof(cur));
	int e2 = rpi_fw_transfer(fw, RPI_FW_TAG_GET_MAX_CLOCK_RATE, max, sizeof(max));

	if (e1 < 0 || e2 < 0) {
		shell_error(sh, "rpi_fw clock query failed: cur=%d max=%d", e1, e2);
		return -EIO;
	}
	shell_print(sh, "ARM clock: cur=%u Hz  max=%u Hz", cur[1], max[1]);
	if (cur[1] < 900000000U) {
		shell_warn(sh, "ARM clock is below 900 MHz (got %u Hz). Set "
				"`arm_freq=1000` + `force_turbo=1` in config.txt to "
				"pin it at 1 GHz.", cur[1]);
	}
	return 0;
}

static int cmd_wipeout_flush(const struct shell *sh, size_t argc, char **argv)
{
	int bands = 4;
	int threshold = 64;

	if (argc < 2) {
		shell_print(sh, "usage: wipeout flush serial|parallel [bands] [threshold]");
		return -EINVAL;
	}
	if (strcmp(argv[1], "serial") == 0) {
		render_software_set_parallel(0, 1, 1);
		shell_print(sh, "render: serial flush");
		return 0;
	}
	if (strcmp(argv[1], "parallel") == 0) {
		extern int gfx_parallel_smp_active(void);

		if (argc >= 3) bands = atoi(argv[2]);
		if (argc >= 4) threshold = atoi(argv[3]);
		render_software_set_parallel(1, bands, threshold);
		shell_print(sh, "render: parallel flush bands=%d threshold=%d  (cpus=%u, smp_active=%d)",
			    bands, threshold,
			    (unsigned)arch_num_cpus(),
			    gfx_parallel_smp_active());
		if (!gfx_parallel_smp_active()) {
			shell_warn(sh, "SMP inactive: secondary cores didn't come up "
					"or build is UP. Workers will share CPU0 and pay "
					"barrier cost for no parallelism.");
		}
		return 0;
	}
	shell_error(sh, "unknown subcommand '%s'", argv[1]);
	return -EINVAL;
}

static int cmd_wipeout_capture(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	if (pizza_capture_requested) {
		shell_print(sh, "capture already pending");
		return 0;
	}
	pizza_capture_requested = 1;
	shell_print(sh, "capture armed: next frame -> /SD:/wipeout-frame.ppm");
	return 0;
}

static int cmd_wipeout_display(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	if (!pizza_display) {
		shell_error(sh, "display device unset");
		return -ENODEV;
	}
	struct display_capabilities caps;

	display_get_capabilities(pizza_display, &caps);
	shell_print(sh, "display: %ux%u px  fmt=0x%x  ready=%d",
		    caps.x_resolution, caps.y_resolution,
		    (unsigned)caps.current_pixel_format,
		    pizza_display_ready);
	return 0;
}

static int cmd_wipeout_smp(const struct shell *sh, size_t argc, char **argv)
{
	extern int gfx_parallel_smp_active(void);

	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_print(sh, "CONFIG_SMP=%s  MP_MAX_NUM_CPUS=%d",
#ifdef CONFIG_SMP
		    "y",
#else
		    "n",
#endif
		    CONFIG_MP_MAX_NUM_CPUS);
	shell_print(sh, "arch_num_cpus() = %u  (cores ONLINE at runtime)",
		    (unsigned)arch_num_cpus());
	shell_print(sh, "gfx_parallel_smp_active() = %d  (renderer's worker-pool view)",
		    gfx_parallel_smp_active());
	return 0;
}

/* Run one bench config. Returns 0 + writes stats on success, -errno on
 * failure. Same engine plumbing as the interactive command path.
 */
static int run_bench_config(int frames, int n_tris, int n_mvps,
			    uint32_t *out_mean_us,
			    uint32_t *out_min_us,
			    uint32_t *out_max_us,
			    uint32_t *out_stat_frames)
{
	if (frames < 1) {
		frames = 1;
	}
	if (frames > 1000) {
		frames = 1000;
	}
	if (n_tris < 1) {
		n_tris = 1;
	}
	if (n_tris > 8000) {
		n_tris = 8000;
	}
	if (n_mvps < 1) {
		n_mvps = 1;
	}
	if (n_mvps > 16) {
		n_mvps = 16;
	}

	for (int i = 0; i < n_mvps; i++) {
		float s = 0.8f + (float)i * 0.1f;
		float tx = -2.0f + (float)i * 0.5f;
		float ty = 1.0f + (float)i * 0.3f;

		pizza_bench_mvps[i] = mat4(
			 s,   0,   0,   0,
			 0,   s,   0,   0,
			 0,   0,   1,   0,
			tx,  ty, -5,    1);
	}

	pizza_bench_n_tris = n_tris;
	pizza_bench_n_mvps = n_mvps;
	pizza_bench_min_cyc = UINT32_MAX;
	pizza_bench_max_cyc = 0;
	pizza_bench_total_cyc = 0;
	pizza_bench_frames_done = 0;
	pizza_bench_stat_frames = 0;
	pizza_bench_done = false;
	pizza_bench_remaining = frames + PIZZA_BENCH_WARMUP_FRAMES;
	pizza_bench_active = true;

	int64_t deadline = k_uptime_get() + 30000;

	while (!pizza_bench_done) {
		if (k_uptime_get() > deadline) {
			pizza_bench_active = false;
			return -ETIMEDOUT;
		}
		k_sleep(K_MSEC(50));
	}

	uint32_t cps = (uint32_t)sys_clock_hw_cycles_per_sec();
	uint32_t nstat = pizza_bench_stat_frames;

	*out_mean_us = nstat
		? (uint32_t)((pizza_bench_total_cyc / nstat) * 1000000ULL / cps)
		: 0u;
	*out_min_us = (uint32_t)((uint64_t)pizza_bench_min_cyc * 1000000ULL / cps);
	*out_max_us = (uint32_t)((uint64_t)pizza_bench_max_cyc * 1000000ULL / cps);
	*out_stat_frames = nstat;
	return 0;
}

/* Predefined matrix for `wipeout bench all`. Six configs covering
 * the speedup curve's interesting points: low/typical/high tri count
 * under both serial and parallel-4 raster. Per-config 30 frames
 * (+2 warmup) -> ~15-30 s wall total at ~50 ms/frame.
 */
struct bench_suite_row {
	const char *label;
	int frames;
	int n_tris;
	int n_mvps;
	int parallel_bands;  /* 0 = serial */
};

static const struct bench_suite_row bench_suite[] = {
	/* QPU win threshold: 1000 tris / 1 mvp = one big batch per finalize. */
	{"parallel-4  1000t / 1m", 30, 1000, 1, 4},
	{"parallel-4  3000t / 3m", 30, 3000, 3, 4},
	{"parallel-4  5000t / 3m", 30, 5000, 3, 4},
	{"parallel-4  7000t / 3m", 30, 7000, 3, 4},
	{"serial      3000t / 3m", 30, 3000, 3, 0},
	{"serial      5000t / 3m", 30, 5000, 3, 0},
};

#define BENCH_SUITE_LEN (sizeof(bench_suite) / sizeof(bench_suite[0]))

static int cmd_wipeout_bench(const struct shell *sh, size_t argc, char **argv)
{
	if (pizza_bench_active) {
		shell_error(sh, "bench already running");
		return -EBUSY;
	}

	bool run_all = (argc > 1) && (strcmp(argv[1], "all") == 0);

	if (run_all) {
		shell_print(sh, "[bench-suite] %u configs (~15-30 s total)",
			    (unsigned)BENCH_SUITE_LEN);
		shell_print(sh,
			    "  %-24s  mean_us   min_us   max_us    fps   frames",
			    "config");

		for (size_t i = 0; i < BENCH_SUITE_LEN; i++) {
			const struct bench_suite_row *row = &bench_suite[i];

			if (row->parallel_bands > 0) {
				render_software_set_parallel(1, row->parallel_bands, 64);
			} else {
				render_software_set_parallel(0, 1, 64);
			}

			uint32_t mean_us = 0, min_us = 0, max_us = 0, nstat = 0;
			int rc = run_bench_config(row->frames, row->n_tris, row->n_mvps,
						  &mean_us, &min_us, &max_us, &nstat);
			if (rc) {
				shell_error(sh, "  %-24s  TIMEOUT/ERROR rc=%d",
					    row->label, rc);
				continue;
			}
			uint32_t fps_x100 = mean_us > 0u
				? (100u * 1000000u / mean_us)
				: 0u;

			shell_print(sh,
				    "  %-24s  %7u  %7u  %7u  %3u.%02u  %5u",
				    row->label, mean_us, min_us, max_us,
				    fps_x100 / 100u, fps_x100 % 100u, nstat);
		}

		shell_print(sh, "[bench-suite] done");
		return 0;
	}

	/* Single-config form: `wipeout bench [frames] [tris] [mvps]`. */
	int frames = (argc > 1) ? atoi(argv[1]) : 60;
	int n_tris = (argc > 2) ? atoi(argv[2]) : 3000;
	int n_mvps = (argc > 3) ? atoi(argv[3]) : 3;

	shell_print(sh, "[bench] %d frames (+%d warmup) x %d tris x %d mvps...",
		    frames, PIZZA_BENCH_WARMUP_FRAMES, n_tris, n_mvps);

	uint32_t mean_us = 0, min_us = 0, max_us = 0, nstat = 0;
	int rc = run_bench_config(frames, n_tris, n_mvps,
				  &mean_us, &min_us, &max_us, &nstat);
	if (rc == -ETIMEDOUT) {
		shell_error(sh, "bench timeout after 30s (engine stuck?)");
		return rc;
	}
	if (rc) {
		shell_error(sh, "bench failed rc=%d", rc);
		return rc;
	}

	uint32_t fps_x100 = mean_us > 0u
		? (100u * 1000000u / mean_us)
		: 0u;

	shell_print(sh, "[bench] tris=%d mvps=%d stat_frames=%u",
		    n_tris, n_mvps, nstat);
	shell_print(sh, "[bench] mean=%u us  min=%u us  max=%u us  fps=%u.%02u",
		    mean_us, min_us, max_us, fps_x100 / 100u, fps_x100 % 100u);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wipeout,
	SHELL_CMD(fps,     NULL, "Sampled fps + per-phase us",          cmd_wipeout_fps),
	SHELL_CMD(clock,   NULL, "VC ARM clock cur/max (Hz)",            cmd_wipeout_clock),
	SHELL_CMD(flush,   NULL, "serial | parallel [bands] [threshold]", cmd_wipeout_flush),
	SHELL_CMD(capture, NULL, "Dump next frame to /DATA:/wipeout-frame.ppm", cmd_wipeout_capture),
	SHELL_CMD(display, NULL, "Display capabilities + ready state",   cmd_wipeout_display),
	SHELL_CMD(smp,     NULL, "SMP build + runtime status",           cmd_wipeout_smp),
	SHELL_CMD(bench,   NULL, "[frames] [tris] [mvps] or 'all' -- deterministic synthetic frame timer", cmd_wipeout_bench),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(wipeout, &sub_wipeout, "PiZZa WipEout demo control", NULL);

/* ------------------------------------------------------------------ *
 *  Zephyr main()
 * ------------------------------------------------------------------ */

int main(void)
{
	printk("\n[wipeout] PiZZa WipEout demo -- rpi_zero_2w SOFTWARE renderer\n");
	printk("[wipeout] SMP: arch_num_cpus()=%u  (MP_MAX_NUM_CPUS=%d)\n",
	       (unsigned)arch_num_cpus(), CONFIG_MP_MAX_NUM_CPUS);

	pizza_display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(pizza_display)) {
		printk("[wipeout] display device not ready -- HDMI not connected?\n");
		return -ENODEV;
	}

	struct display_capabilities caps;

	display_get_capabilities(pizza_display, &caps);
	pizza_display_ready = 1;
	printk("[wipeout] display %ux%u (HVS virt) fmt=0x%x\n",
	       caps.x_resolution, caps.y_resolution,
	       (unsigned)caps.current_pixel_format);

	if (caps.x_resolution != PIZZA_W || caps.y_resolution != PIZZA_H) {
		printk("[wipeout] WARNING: display reports %ux%u, expected %ux%u "
		       "(check render-width/render-height in overlay)\n",
		       caps.x_resolution, caps.y_resolution, PIZZA_W, PIZZA_H);
	}

	/* Spin the band-worker pool now (even on UP -- it's harmless and
	 * lets the render loop assume gfx_parallel_run is available).
	 * M5 benchmark switches between serial / parallel via the shell.
	 */
	extern void gfx_parallel_init(int nbands);
	gfx_parallel_init(4);

	k_thread_create(&pizza_game_thread, pizza_game_stack, GAME_STACK_SIZE,
			pizza_game_loop, NULL, NULL, NULL,
			K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	k_thread_name_set(&pizza_game_thread, "wipeout_game");

	/* main() returns; the game thread + shell continue. */
	return 0;
}
