/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Offline host test for shim_mixer.c (work-order G3), same idea as
 * SDLPoP's tests/audio_host: compile the mixer against host stubs for
 * the sdl2shim audio seam, drive it through the Mix_ API, and check the
 * summed callback output for gain, panning, clipping, effects and
 * one-shot end. No hardware, no Zephyr.
 *
 * The mixer registers its callback via s2s_audio_open(); the stub below
 * captures it so the harness can invoke it directly (as the MAI feeder
 * would on hardware).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "sdl2shim.h"
#include <SDL2/SDL_mixer.h>

/* ── host stubs for the sdl2shim seams shim_mixer.c calls ──────────── */

static void (*g_cb)(void *, Uint8 *, int);
static void *g_cb_user;

int s2s_audio_open(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	g_cb = desired->callback;
	g_cb_user = desired->userdata;
	if (obtained != NULL) {
		*obtained = *desired;
	}
	return 0;
}
void s2s_audio_close(void) { g_cb = NULL; }
void s2s_audio_pause(int p) { (void)p; }
void s2s_audio_lock(void) {}
void s2s_audio_unlock(void) {}
SDL_AudioStatus s2s_audio_status(void) { return SDL_AUDIO_PLAYING; }
void s2s_set_error(const char *fmt, ...) { (void)fmt; }

/* ── harness ───────────────────────────────────────────────────────── */

#define BLK 64 /* frames per render */

static int n_run, n_fail;

static void check(int cond, const char *msg)
{
	n_run++;
	if (!cond) {
		n_fail++;
		printf("  FAIL: %s\n", msg);
	} else {
		printf("  ok:   %s\n", msg);
	}
}

static int16_t out[BLK * 2];

static void render(void)
{
	g_cb(g_cb_user, (Uint8 *)out, BLK * 2 * (int)sizeof(int16_t));
}

static int16_t bufA[256 * 2], bufB[256 * 2], bufShort[32 * 2];
static Mix_Chunk chunkA, chunkB, chunkShort;

static void fill(int16_t *buf, int frames, int16_t val)
{
	for (int i = 0; i < frames * 2; i++) {
		buf[i] = val;
	}
}

static void reset_all(void)
{
	Mix_HaltChannel(-1);
	Mix_UnregisterAllEffects(0);
	Mix_UnregisterAllEffects(1);
	Mix_UnregisterAllEffects(MIX_CHANNEL_POST);
	Mix_SetPanning(0, 255, 255);
	Mix_SetPanning(1, 255, 255);
	chunkA.volume = 128;
	chunkB.volume = 128;
	chunkShort.volume = 128;
	memset(out, 0, sizeof(out));
}

static void fx_zero(int chan, void *stream, int len, void *udata)
{
	(void)chan;
	(void)udata;
	memset(stream, 0, len);
}

static void fx_halve(int chan, void *stream, int len, void *udata)
{
	(void)chan;
	(void)udata;
	int16_t *s = stream;

	for (int i = 0; i < len / (int)sizeof(int16_t); i++) {
		s[i] = (int16_t)(s[i] / 2);
	}
}

int main(void)
{
	fill(bufA, 128, 1000);
	chunkA = (Mix_Chunk){ 0, (Uint8 *)bufA, 128 * 2 * 2, 128 };
	fill(bufB, 128, 1000);
	chunkB = (Mix_Chunk){ 0, (Uint8 *)bufB, 128 * 2 * 2, 128 };
	fill(bufShort, 32, 1000);
	chunkShort = (Mix_Chunk){ 0, (Uint8 *)bufShort, 32 * 2 * 2, 128 };

	printf("mixer_host: shim_mixer offline tests\n");

	check(Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 512) == 0,
	      "Mix_OpenAudio 44100/S16/2 succeeds");

	int freq = 0, chans = 0;
	Uint16 fmt = 0;

	check(Mix_QuerySpec(&freq, &fmt, &chans) == 1 && freq == 44100 &&
	      fmt == AUDIO_S16SYS && chans == 2,
	      "Mix_QuerySpec echoes 44100/S16/2");
	check(Mix_AllocateChannels(8) == 8, "Mix_AllocateChannels(8) == 8");

	/* 1: unity gain, center pan -> output equals the source samples */
	reset_all();
	check(Mix_PlayChannelTimed(0, &chunkA, 0, -1) == 0,
	      "PlayChannelTimed(ch0) returns 0");
	render();
	check(out[0] == 1000 && out[1] == 1000 && out[2 * (BLK - 1)] == 1000,
	      "unity: out == source");

	/* 2: half chunk volume */
	reset_all();
	chunkA.volume = 64;
	Mix_PlayChannelTimed(0, &chunkA, 0, -1);
	render();
	check(out[0] == 500, "volume 64/128 -> source/2");

	/* 3: hard-left panning */
	reset_all();
	Mix_PlayChannelTimed(0, &chunkA, 0, -1);
	Mix_SetPanning(0, 255, 0);
	render();
	check(out[0] == 1000 && out[1] == 0, "pan left: L == source, R == 0");

	/* 4: two channels sum with saturation */
	reset_all();
	fill(bufA, 128, 20000);
	fill(bufB, 128, 20000);
	Mix_PlayChannelTimed(0, &chunkA, 0, -1);
	Mix_PlayChannelTimed(1, &chunkB, 0, -1);
	render();
	check(out[0] == 32767, "20000 + 20000 clips to 32767");
	fill(bufA, 128, 1000);
	fill(bufB, 128, 1000);

	/* 5: per-channel effect runs on the channel stream */
	reset_all();
	Mix_PlayChannelTimed(0, &chunkA, 0, -1);
	Mix_RegisterEffect(0, fx_zero, NULL, NULL);
	render();
	check(out[0] == 0, "per-channel effect zeroes the stream");

	/* 6: one-shot ends mid-block, channel stops, then silence */
	reset_all();
	Mix_PlayChannelTimed(0, &chunkShort, 0, -1);
	render();
	check(out[0] == 1000 && out[2 * 32] == 0,
	      "short chunk: 32 frames then zero-pad");
	check(Mix_Playing(0) == 0, "channel stops after one-shot");
	render();
	check(out[0] == 0, "silence after stop");

	/* 7: post-mix effect (MIX_CHANNEL_POST) on the final block */
	reset_all();
	Mix_PlayChannelTimed(0, &chunkA, 0, -1);
	Mix_RegisterEffect(MIX_CHANNEL_POST, fx_halve, NULL, NULL);
	render();
	check(out[0] == 500, "post effect halves the final mix");

	printf("mixer_host: %d checks, %d failed\n", n_run, n_fail);
	return n_fail ? 1 : 0;
}
