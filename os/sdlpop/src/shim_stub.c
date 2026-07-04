/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- deferred-scope buckets (work-order §2 C+D).
 *
 * Audio (bucket C): entry points delegate to the pop_audio_* backend
 * seam; the Kconfig-selected backend today is shim_audio_none.c.
 * SDL_OpenAudio therefore fails, which flips SDLPoP's own
 * digi_unavailable switch and cleanly disables every audio path in
 * the game (no sound loading, no callback machinery). When the I2S
 * backend lands (§5a) it implements the same six functions and
 * OpenAudio starts succeeding -- no game or shim changes.
 *
 * Pads/haptics (bucket D): permanently "none present" -- the 8BitDo
 * Micro arrives as a HOGP *keyboard* through the event-queue seam,
 * so SDLPoP's controller path never activates.
 */

#include <zephyr/kernel.h>

#include "pop_shim.h"

/* ── audio entry points -> backend seam ──────────────────────── */

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	return pop_audio_open(desired, obtained);
}

void SDL_CloseAudio(void)
{
	pop_audio_close();
}

void SDL_PauseAudio(int pause_on)
{
	pop_audio_pause(pause_on);
}

void SDL_LockAudio(void)
{
	pop_audio_lock();
}

void SDL_UnlockAudio(void)
{
	pop_audio_unlock();
}

SDL_AudioStatus SDL_GetAudioStatus(void)
{
	return pop_audio_status();
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
		      SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
		      SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate)
{
	memset(cvt, 0, sizeof(*cvt));
	cvt->src_format = src_format;
	cvt->dst_format = dst_format;
	cvt->len_mult = 1;
	cvt->len_ratio = 1.0;
	cvt->rate_incr = 1.0;

	if (src_format == dst_format && src_channels == dst_channels &&
	    src_rate == dst_rate) {
		return 0;   /* no conversion needed */
	}
	/* Conversion is an audio-backend concern; unreachable while
	 * OpenAudio fails.
	 */
	pop_set_error("BuildAudioCVT: conversion not supported");
	return -1;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
	cvt->len_cvt = cvt->len;
	return 0;
}

/* ── joystick / controller / haptic: none present, uniformly ─── */

int SDL_NumJoysticks(void)
{
	return 0;
}

SDL_bool SDL_IsGameController(int joystick_index)
{
	ARG_UNUSED(joystick_index);
	return SDL_FALSE;
}

SDL_GameController *SDL_GameControllerOpen(int joystick_index)
{
	ARG_UNUSED(joystick_index);
	return NULL;
}

void SDL_GameControllerClose(SDL_GameController *gamecontroller)
{
	ARG_UNUSED(gamecontroller);
}

SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID joyid)
{
	ARG_UNUSED(joyid);
	return NULL;
}

int SDL_GameControllerAddMappingsFromFile(const char *file)
{
	ARG_UNUSED(file);
	return 0;
}

int SDL_GameControllerRumble(SDL_GameController *gamecontroller,
			     Uint16 low_frequency_rumble, Uint16 high_frequency_rumble,
			     Uint32 duration_ms)
{
	ARG_UNUSED(gamecontroller);
	ARG_UNUSED(low_frequency_rumble);
	ARG_UNUSED(high_frequency_rumble);
	ARG_UNUSED(duration_ms);
	return -1;
}

SDL_Joystick *SDL_JoystickOpen(int device_index)
{
	ARG_UNUSED(device_index);
	return NULL;
}

int SDL_JoystickRumble(SDL_Joystick *joystick,
		       Uint16 low_frequency_rumble, Uint16 high_frequency_rumble,
		       Uint32 duration_ms)
{
	ARG_UNUSED(joystick);
	ARG_UNUSED(low_frequency_rumble);
	ARG_UNUSED(high_frequency_rumble);
	ARG_UNUSED(duration_ms);
	return -1;
}

SDL_Haptic *SDL_HapticOpen(int device_index)
{
	ARG_UNUSED(device_index);
	return NULL;
}

int SDL_HapticRumbleInit(SDL_Haptic *haptic)
{
	ARG_UNUSED(haptic);
	return -1;
}

int SDL_HapticRumblePlay(SDL_Haptic *haptic, float strength, Uint32 length)
{
	ARG_UNUSED(haptic);
	ARG_UNUSED(strength);
	ARG_UNUSED(length);
	return -1;
}
