/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sdl2shim-over-libretro -- audio backend for core builds.
 *
 * Implements the s2s_audio_* seam: the game's SDL pull-callback is
 * drained by the glue once per retro_run (one video frame's worth of
 * samples) and handed to the frontend's audio_batch callback. Same
 * fixed contract as the HDMI backend: 44100 Hz / S16 / stereo.
 * A game that never opens audio costs nothing (the pump no-ops).
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "sdl2shim.h"
#include "sdl2shim_libretro.h"

#define LR_AUDIO_MAX_FRAMES 2048

static SDL_AudioSpec cur_spec;
static bool is_open;
static bool is_paused = true;

/* Frontend-donated (kernel-object ABI hygiene -- see shim_libretro.c). */
extern struct k_mutex s2s_lr_audio_mutex;
#define audio_lock s2s_lr_audio_mutex

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

size_t s2s_audio_lr_pump(size_t (*batch)(const int16_t *, size_t),
			 double fps)
{
	static int16_t buf[LR_AUDIO_MAX_FRAMES * 2];

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
