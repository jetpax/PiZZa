/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- OPL glue (CONFIG_DOOM_MUSIC_OPL).
 *
 * The handful of SDL/Mix primitives Chocolate Doom's OPL subsystem
 * (opl/opl.c + opl/opl_sdl.c) needs that the shim did not already have:
 * SDL mutex/condvar over Zephyr kernel objects, the saturating
 * SDL_MixAudioFormat that folds the synth output into the post-mix block,
 * and stubs for the two Mix entry points on paths the OPL driver does not
 * take here (Mix is already open -- the SFX mixer owns it).
 */

#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>

#include "sdl2shim.h"
#include <SDL2/SDL_mixer.h>

struct SDL_mutex {
	struct k_mutex m;
};

struct SDL_cond {
	struct k_condvar cv;
};

SDL_mutex *SDL_CreateMutex(void)
{
	SDL_mutex *mutex = malloc(sizeof(*mutex));

	if (mutex != NULL) {
		k_mutex_init(&mutex->m);
	}
	return mutex;
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
	free(mutex);
}

int SDL_LockMutex(SDL_mutex *mutex)
{
	return k_mutex_lock(&mutex->m, K_FOREVER);
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
	return k_mutex_unlock(&mutex->m);
}

SDL_cond *SDL_CreateCond(void)
{
	SDL_cond *cond = malloc(sizeof(*cond));

	if (cond != NULL) {
		k_condvar_init(&cond->cv);
	}
	return cond;
}

void SDL_DestroyCond(SDL_cond *cond)
{
	free(cond);
}

int SDL_CondSignal(SDL_cond *cond)
{
	return k_condvar_signal(&cond->cv);
}

int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex)
{
	return k_condvar_wait(&cond->cv, &mutex->m, K_FOREVER);
}

void SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src, SDL_AudioFormat format,
			Uint32 len, int volume)
{
	int16_t *d = (int16_t *)dst;
	const int16_t *s = (const int16_t *)src;
	unsigned int n = len / sizeof(int16_t);

	ARG_UNUSED(format); /* Doom OPL output is always AUDIO_S16SYS */

	for (unsigned int i = 0; i < n; i++) {
		int32_t v = d[i] + (s[i] * volume) / SDL_MIX_MAXVOLUME;

		d[i] = (int16_t)CLAMP(v, -32768, 32767);
	}
}

int Mix_OpenAudioDevice(int frequency, Uint16 format, int channels,
			int chunksize, const char *device, int allowed_changes)
{
	ARG_UNUSED(frequency);
	ARG_UNUSED(format);
	ARG_UNUSED(channels);
	ARG_UNUSED(chunksize);
	ARG_UNUSED(device);
	ARG_UNUSED(allowed_changes);

	/* Never reached: the SFX mixer already opened audio, so opl_sdl.c's
	 * !SDLIsInitialized() branch is not taken. Fail if that ever breaks.
	 */
	return -1;
}

void Mix_HookMusic(Mix_MusicHook mix_func, void *arg)
{
	ARG_UNUSED(mix_func);
	ARG_UNUSED(arg);
}

/* ── Doom helpers the newer Chocolate OPL/MIDI code calls that the older
 *    sdl2-doom fork does not provide ──────────────────────────────────
 */

/* DEH: the fork stubs dehacked; string substitution is a passthrough. */
const char *DEH_String(const char *s)
{
	return s;
}

/* Plain passthrough. i_oplmusic round-trips each song's converted MIDI
 * through a temp file (M_TempFile "doom.mid" -> M_WriteFile -> MIDI_LoadFile);
 * both the fork's M_WriteFile and Chocolate's MIDI_LoadFile funnel through
 * open()/read()/write(), so the RAM temp file lives in the fd layer
 * (doom_assets.c) where both sides see the same buffer.
 */
FILE *M_fopen(const char *filename, const char *mode)
{
	return fopen(filename, mode);
}

int M_remove(const char *path)
{
	return remove(path);
}

void *I_Realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}
