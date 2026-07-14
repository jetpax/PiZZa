/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sdl2shim-over-libretro -- audio backend for core builds.
 *
 * Implements the s2s_audio_* seam. Two production models:
 *
 *  - Producer thread (M6.6, the default once the frontend supplies a
 *    batch callback): a dedicated, donated thread mixes the game's SDL
 *    pull-callback in small chunks and pushes them to the frontend's
 *    audio_batch callback, paced by backpressure (a full frontend ring
 *    rejects the chunk; the producer sleeps and retries the SAME chunk,
 *    so nothing is lost). The thread runs ABOVE the game in priority:
 *    audio production preempts game compute instead of starving behind
 *    it when a frame runs over budget -- the architecture the standalone
 *    apps proved (their MAI feeder pulled the mixer the same way).
 *
 *  - Legacy inline pump (s2s_audio_lr_pump, no-op while the producer
 *    runs): retro_run drains one video frame's worth per call. Kept for
 *    cores built from older glue.
 *
 * Fixed contract either way: 44100 Hz / S16 / stereo. A game that never
 * opens audio costs nothing (the producer sleeps).
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "sdl2shim.h"
#include "sdl2shim_libretro.h"

#define LR_AUDIO_MAX_FRAMES 2048
#define LR_PROD_CHUNK       256 /* frames per producer pass (~5.8 ms) */

static SDL_AudioSpec cur_spec;
static bool is_open;
static bool is_paused = true;

/* Frontend-donated (kernel-object ABI hygiene -- see shim_libretro.c). */
extern struct k_mutex s2s_lr_audio_mutex;
extern struct k_thread s2s_lr_audio_thread;
extern struct z_thread_stack_element s2s_lr_audio_stack[];
extern const size_t s2s_lr_audio_stack_size;

#define audio_lock s2s_lr_audio_mutex

static size_t (*prod_batch)(const int16_t *, size_t);
static volatile bool prod_run;
static bool prod_started;

int s2s_audio_open(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	if (desired == NULL || desired->callback == NULL) {
		return -1;
	}
	if (desired->freq != 44100 || desired->channels != 2 ||
	    desired->format != AUDIO_S16SYS) {
		return -1;
	}

	k_mutex_init(&audio_lock);
	cur_spec = *desired;
	if (obtained != NULL) {
		*obtained = *desired;
	}
	is_open = true;
	is_paused = true;
	return 0;
}

void s2s_audio_close(void)
{
	is_open = false;
	is_paused = true;
}

void s2s_audio_pause(int pause_on)
{
	is_paused = (pause_on != 0);
}

void s2s_audio_lock(void)
{
	if (is_open) {
		k_mutex_lock(&audio_lock, K_FOREVER);
	}
}

void s2s_audio_unlock(void)
{
	if (is_open) {
		k_mutex_unlock(&audio_lock);
	}
}

SDL_AudioStatus s2s_audio_status(void)
{
	if (!is_open) {
		return SDL_AUDIO_STOPPED;
	}
	return is_paused ? SDL_AUDIO_PAUSED : SDL_AUDIO_PLAYING;
}

/* ── producer thread ──────────────────────────────────────────────── */

static void producer_fn(void *a, void *b, void *c)
{
	static int16_t buf[LR_PROD_CHUNK * 2];
	size_t pending = 0;
	int64_t epoch_ms = 0;
	uint64_t produced = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (prod_run) {
		if (!is_open || is_paused || prod_batch == NULL) {
			/* Nothing to produce; the frontend ring drains and its
			 * feeder pads silence (SDL pause semantics for free).
			 */
			epoch_ms = 0;
			k_msleep(3);
			continue;
		}

		/* Self-clock to realtime + a small cushion: a frontend that
		 * accepts everything (qemu drops audio) must not let this
		 * thread mix unboundedly and starve the game below it. On
		 * hardware the frontend's ~35 ms accept limit is the finer
		 * (DMA-derived) pace; this is the coarse ceiling.
		 */
		if (epoch_ms == 0) {
			epoch_ms = k_uptime_get();
			produced = 0;
		}
		uint64_t due = (uint64_t)(k_uptime_get() - epoch_ms) * 441U / 10U
			       + 2U * LR_PROD_CHUNK;
		if (produced >= due) {
			k_msleep(2);
			continue;
		}

		if (pending == 0) {
			k_mutex_lock(&audio_lock, K_FOREVER);
			cur_spec.callback(cur_spec.userdata, (Uint8 *)buf,
					  (int)sizeof(buf));
			k_mutex_unlock(&audio_lock);
			pending = LR_PROD_CHUNK;
		}

		if (prod_batch(buf, pending) == pending) {
			pending = 0; /* accepted whole */
			produced += LR_PROD_CHUNK;
		} else {
			/* Frontend ring at its latency target (all-or-nothing):
			 * enough is buffered. Sleep and retry the SAME chunk --
			 * the backpressure IS the audio-clock pacing.
			 */
			k_msleep(3);
		}
	}
}

void s2s_audio_lr_producer_start(size_t (*batch)(const int16_t *, size_t))
{
	if (batch == NULL) {
		return;
	}
	prod_batch = batch;
	if (prod_started) {
		return;
	}

	prod_run = true;
	k_thread_create(&s2s_lr_audio_thread, s2s_lr_audio_stack,
			s2s_lr_audio_stack_size, producer_fn,
			NULL, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_thread_name_set(&s2s_lr_audio_thread, "lr_audio");
	prod_started = true;
}

void s2s_audio_lr_producer_stop(void)
{
	if (!prod_started) {
		return;
	}
	prod_run = false;
	/* Loop sleeps <= 3 ms between passes; join is prompt. */
	if (k_thread_join(&s2s_lr_audio_thread, K_MSEC(500)) != 0) {
		k_thread_abort(&s2s_lr_audio_thread);
	}
	prod_started = false;
	prod_batch = NULL;
}

/* ── legacy inline pump (older cores; no-op while the producer runs) ── */

size_t s2s_audio_lr_pump(size_t (*batch)(const int16_t *, size_t),
			 double fps)
{
	static int16_t buf[LR_AUDIO_MAX_FRAMES * 2];

	if (prod_started) {
		return 0;
	}
	if (!is_open || is_paused || batch == NULL || fps <= 1.0) {
		return 0;
	}

	size_t frames = (size_t)(44100.0 / fps + 0.5);

	if (frames > LR_AUDIO_MAX_FRAMES) {
		frames = LR_AUDIO_MAX_FRAMES;
	}

	k_mutex_lock(&audio_lock, K_FOREVER);
	cur_spec.callback(cur_spec.userdata, (Uint8 *)buf,
			  (int)(frames * 2 * sizeof(int16_t)));
	k_mutex_unlock(&audio_lock);

	return batch(buf, frames);
}
