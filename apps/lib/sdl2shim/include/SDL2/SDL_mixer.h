/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- minimal SDL_mixer surface for sdl2-doom.
 *
 * Declares exactly the Mix_ symbols the game references (grep-verified
 * against i_sdlsound.c / i_sdlmusic.c). The SFX subset is implemented
 * by a small multi-channel PCM mixer (apps/Doom/src/shim_mixer.c) over
 * the sdl2shim audio seam (s2s_audio_*); music is stubbed, so Doom
 * plays with silent music but is fully functional. See
 * apps/Doom/notes/WORK_ORDER.md §6.
 *
 * Mix_Chunk layout matches SDL_mixer's exactly -- Doom builds chunks by
 * filling abuf/alen/volume itself (i_sdlsound.c getsfx()), never via
 * Mix_LoadWAV, so the fields are a hard ABI contract.
 */

#ifndef PIZZA_SDL2_SDL_MIXER_H
#define PIZZA_SDL2_SDL_MIXER_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MIXER_MAJOR_VERSION 2
#define SDL_MIXER_MINOR_VERSION 0
#define SDL_MIXER_PATCHLEVEL    4

#define SDL_MIXER_VERSION(X)                             \
	do {                                             \
		(X)->major = SDL_MIXER_MAJOR_VERSION;    \
		(X)->minor = SDL_MIXER_MINOR_VERSION;    \
		(X)->patch = SDL_MIXER_PATCHLEVEL;       \
	} while (0)

const SDL_version *Mix_Linked_Version(void);

#define MIX_DEFAULT_FREQUENCY 22050
#define MIX_DEFAULT_FORMAT    AUDIO_S16SYS
#define MIX_DEFAULT_CHANNELS  2
#define MIX_MAX_VOLUME        128

/* Special channel id for a post-mix effect (Mix_RegisterEffect). */
#define MIX_CHANNEL_POST (-2)

typedef struct Mix_Chunk {
	int    allocated;
	Uint8 *abuf;    /* PCM in the Mix_QuerySpec format */
	Uint32 alen;    /* bytes */
	Uint8  volume;  /* 0..MIX_MAX_VOLUME */
} Mix_Chunk;

typedef struct _Mix_Music Mix_Music;

typedef void (*Mix_EffectFunc_t)(int chan, void *stream, int len, void *udata);
typedef void (*Mix_EffectDone_t)(int chan, void *udata);

/* SDL_mixer routes its errors through SDL's error buffer. */
#define Mix_GetError SDL_GetError

/* ── SFX subset -- implemented (shim_mixer.c) ────────────────────── */

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
void Mix_CloseAudio(void);
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
int Mix_AllocateChannels(int numchans);
int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks);
#define Mix_PlayChannel(channel, chunk, loops) \
	Mix_PlayChannelTimed(channel, chunk, loops, -1)
int Mix_HaltChannel(int channel);
int Mix_Playing(int channel);
int Mix_SetPanning(int channel, Uint8 left, Uint8 right);
int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg);
int Mix_UnregisterAllEffects(int channel);

/* ── music subset -- stubbed (shim_mixer.c) ──────────────────────── */

Mix_Music *Mix_LoadMUS(const char *file);
Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc);
void Mix_FreeMusic(Mix_Music *music);
int Mix_PlayMusic(Mix_Music *music, int loops);
int Mix_HaltMusic(void);
int Mix_PlayingMusic(void);
int Mix_VolumeMusic(int volume);
int Mix_SetMusicPosition(double position);
int Mix_SetMusicCMD(const char *command);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2_SDL_MIXER_H */
