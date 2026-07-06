/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- SDL_mixer shim: a small multi-channel PCM mixer.
 *
 * Doom's i_sdlsound.c opens SDL_mixer at (snd_samplerate, AUDIO_S16SYS,
 * 2), reads the real rate back via Mix_QuerySpec into mixer_freq, and
 * resamples every DMX lump to mixer_freq when it builds a Mix_Chunk.
 * So the SFX rate is ours: we advertise the sdl2shim audio backend's
 * fixed 44100 (matched by the HDMI/MAI backend), Doom hands us native
 * 44.1 kHz S16 stereo chunks, and this mixer sums them 1:1 into the
 * audio callback -- no resampler on the SFX path here (the backend does
 * the single 44.1->48 kHz step shared with SDLPoP).
 *
 * The mixer IS the SDL audio callback: Mix_OpenAudio registers
 * mixer_callback() with s2s_audio_open(); on hardware the MAI feeder
 * pulls it under the audio lock, on qemu the "none" backend fails open
 * and Doom runs silent. Music is stubbed (§6b): Doom plays with silent
 * music, fully functional; OPL synthesis is a later, optional channel.
 *
 * Concurrency: Mix_* mutations take s2s_audio_lock() (the backend's
 * mutex); the callback already runs under that lock (the feeder holds
 * it across the callback), so it does not re-lock.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "sdl2shim.h"
#include <SDL2/SDL_mixer.h>

LOG_MODULE_REGISTER(doom_mixer, CONFIG_SDL2SHIM_LOG_LEVEL);

#define MIX_MAX_CHANNELS   16
#define MIX_FX_PER_CHANNEL 4
#define MIX_BLOCK_MAX      2048 /* frames; backend blocks are ~176 */

struct effect {
	Mix_EffectFunc_t func;
	Mix_EffectDone_t done;
	void *udata;
	bool used;
};

struct channel {
	Mix_Chunk *chunk;
	uint32_t pos;       /* byte offset into chunk->abuf */
	int loops;          /* remaining repeats; -1 = forever */
	bool playing;
	Uint8 left, right;  /* Mix_SetPanning, 0..255 */
	struct effect fx[MIX_FX_PER_CHANNEL];
};

static struct channel channels[MIX_MAX_CHANNELS];
static struct effect post_fx[MIX_FX_PER_CHANNEL];
static int num_channels;

static bool mix_open;
static int mix_freq;
static Uint16 mix_format;
static int mix_chans;

/* Callback scratch: one channel's block, isolated so effects can run
 * on it before it is summed. Sized for the largest block we accept.
 */
static int16_t scratch[MIX_BLOCK_MAX * 2];

/* Diagnostics for `doom mix`: is Doom actually feeding the mixer, with
 * valid chunks, and do sounds finish instantly? Globals so the shell can
 * read them; the whole SFX path first executes on hardware.
 */
uint32_t mixer_play_calls;
uint32_t mixer_last_alen;
int mixer_last_chan = -1;
uint32_t mixer_finishes;

/* ── the audio callback (mixer core) ─────────────────────────────── */

static void run_effects(struct effect *fx, int chan, int16_t *buf, int len_bytes)
{
	for (int e = 0; e < MIX_FX_PER_CHANNEL; e++) {
		if (fx[e].used && fx[e].func != NULL) {
			fx[e].func(chan, buf, len_bytes, fx[e].udata);
		}
	}
}

static void mixer_callback(void *userdata, Uint8 *stream, int len)
{
	ARG_UNUSED(userdata);

	int16_t *out = (int16_t *)stream;
	int frames = len / (int)(2 * sizeof(int16_t));

	memset(stream, 0, (size_t)len);

	if (frames > MIX_BLOCK_MAX) {
		frames = MIX_BLOCK_MAX; /* clamp; backend never asks this much */
	}

	for (int c = 0; c < num_channels; c++) {
		struct channel *ch = &channels[c];

		if (!ch->playing || ch->chunk == NULL || ch->chunk->abuf == NULL) {
			continue;
		}

		int f = 0;

		while (f < frames && ch->playing) {
			uint32_t remain = ch->chunk->alen - ch->pos;
			int avail = (int)(remain / (2 * sizeof(int16_t)));
			int take = MIN(frames - f, avail);

			if (take > 0) {
				memcpy(&scratch[f * 2],
				       ch->chunk->abuf + ch->pos,
				       (size_t)take * 2 * sizeof(int16_t));
				ch->pos += (uint32_t)take * 2 * sizeof(int16_t);
				f += take;
			}

			if (ch->pos >= ch->chunk->alen) {
				if (ch->loops == 0) {
					ch->playing = false;
					mixer_finishes++;
				} else {
					if (ch->loops > 0) {
						ch->loops--;
					}
					ch->pos = 0;
				}
			}
		}

		/* zero-pad the tail if the sound ended mid-block */
		if (f < frames) {
			memset(&scratch[f * 2], 0,
			       (size_t)(frames - f) * 2 * sizeof(int16_t));
		}

		/* per-channel: registered effects (Doom's positional filter),
		 * then panning * chunk volume, then sum with clip.
		 */
		run_effects(ch->fx, c, scratch, frames * (int)(2 * sizeof(int16_t)));

		int vol = ch->chunk->volume; /* 0..128 */

		for (int i = 0; i < frames; i++) {
			int l = scratch[2 * i];
			int r = scratch[2 * i + 1];

			l = (l * vol * ch->left) / (MIX_MAX_VOLUME * 255);
			r = (r * vol * ch->right) / (MIX_MAX_VOLUME * 255);

			int sum_l = out[2 * i] + l;
			int sum_r = out[2 * i + 1] + r;

			out[2 * i] = (int16_t)CLAMP(sum_l, -32768, 32767);
			out[2 * i + 1] = (int16_t)CLAMP(sum_r, -32768, 32767);
		}
	}

	/* post-mix effects on the final stereo block (MIX_CHANNEL_POST) */
	run_effects(post_fx, MIX_CHANNEL_POST, out, len);
}

/* ── open / close ────────────────────────────────────────────────── */

int Mix_OpenAudio(int frequency, Uint16 format, int n_channels, int chunksize)
{
	if (mix_open) {
		s2s_set_error("Mix_OpenAudio: already open");
		return -1;
	}

	SDL_AudioSpec desired = {
		.freq = frequency,
		.format = format,
		.channels = (Uint8)n_channels,
		.samples = (Uint16)chunksize,
		.callback = mixer_callback,
		.userdata = NULL,
	};
	SDL_AudioSpec obtained;

	if (s2s_audio_open(&desired, &obtained) != 0) {
		/* error already set by the backend */
		return -1;
	}

	mix_freq = obtained.freq;
	mix_format = obtained.format;
	mix_chans = obtained.channels;
	mix_open = true;

	/* SDL_mixer allocates 8 channels by default at open. */
	num_channels = 8;
	for (int c = 0; c < MIX_MAX_CHANNELS; c++) {
		channels[c].left = 255;
		channels[c].right = 255;
	}

	LOG_INF("Mix_OpenAudio: %d Hz, fmt 0x%04x, %d ch (requested %d)",
		mix_freq, mix_format, mix_chans, frequency);
	return 0;
}

void Mix_CloseAudio(void)
{
	if (!mix_open) {
		return;
	}
	s2s_audio_close();
	mix_open = false;
	num_channels = 0;
}

int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
	if (!mix_open) {
		return 0;
	}
	if (frequency != NULL) {
		*frequency = mix_freq;
	}
	if (format != NULL) {
		*format = mix_format;
	}
	if (channels != NULL) {
		*channels = mix_chans;
	}
	return 1;
}

int Mix_AllocateChannels(int numchans)
{
	if (numchans < 0) {
		return num_channels; /* query */
	}

	s2s_audio_lock();
	if (numchans > MIX_MAX_CHANNELS) {
		numchans = MIX_MAX_CHANNELS;
	}
	/* shrinking silences the dropped channels */
	for (int c = numchans; c < num_channels; c++) {
		channels[c].playing = false;
		channels[c].chunk = NULL;
	}
	num_channels = numchans;
	s2s_audio_unlock();

	return num_channels;
}

/* ── playback ────────────────────────────────────────────────────── */

int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks)
{
	ARG_UNUSED(ticks); /* max-duration cap unused by Doom (passes -1) */

	if (chunk == NULL) {
		s2s_set_error("Mix_PlayChannelTimed: NULL chunk");
		return -1;
	}

	s2s_audio_lock();

	if (channel < 0) {
		for (int c = 0; c < num_channels; c++) {
			if (!channels[c].playing) {
				channel = c;
				break;
			}
		}
	}
	if (channel < 0 || channel >= num_channels) {
		s2s_audio_unlock();
		s2s_set_error("Mix_PlayChannelTimed: no free channel");
		return -1;
	}

	struct channel *ch = &channels[channel];

	ch->chunk = chunk;
	ch->pos = 0;
	ch->loops = loops;
	ch->playing = true;

	mixer_play_calls++;
	mixer_last_alen = chunk->alen;
	mixer_last_chan = channel;
	s2s_audio_unlock();

	return channel;
}

int Mix_HaltChannel(int channel)
{
	s2s_audio_lock();
	if (channel < 0) {
		for (int c = 0; c < num_channels; c++) {
			channels[c].playing = false;
		}
	} else if (channel < num_channels) {
		channels[channel].playing = false;
	}
	s2s_audio_unlock();
	return 0;
}

int Mix_Playing(int channel)
{
	if (channel < 0) {
		int n = 0;

		for (int c = 0; c < num_channels; c++) {
			if (channels[c].playing) {
				n++;
			}
		}
		return n;
	}
	if (channel >= num_channels) {
		return 0;
	}
	return channels[channel].playing ? 1 : 0;
}

int Mix_SetPanning(int channel, Uint8 left, Uint8 right)
{
	if (channel < 0 || channel >= num_channels) {
		return 0;
	}
	s2s_audio_lock();
	channels[channel].left = left;
	channels[channel].right = right;
	s2s_audio_unlock();
	return 1;
}

/* ── effects ─────────────────────────────────────────────────────── */

static struct effect *effect_table(int chan, int *count)
{
	if (chan == MIX_CHANNEL_POST) {
		*count = MIX_FX_PER_CHANNEL;
		return post_fx;
	}
	if (chan >= 0 && chan < num_channels) {
		*count = MIX_FX_PER_CHANNEL;
		return channels[chan].fx;
	}
	return NULL;
}

int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg)
{
	int count;
	struct effect *tbl = effect_table(chan, &count);

	if (tbl == NULL) {
		s2s_set_error("Mix_RegisterEffect: bad channel %d", chan);
		return 0;
	}

	s2s_audio_lock();
	for (int e = 0; e < count; e++) {
		if (!tbl[e].used) {
			tbl[e].func = f;
			tbl[e].done = d;
			tbl[e].udata = arg;
			tbl[e].used = true;
			s2s_audio_unlock();
			return 1;
		}
	}
	s2s_audio_unlock();
	s2s_set_error("Mix_RegisterEffect: effect table full");
	return 0;
}

int Mix_UnregisterAllEffects(int channel)
{
	int count;
	struct effect *tbl = effect_table(channel, &count);

	if (tbl == NULL) {
		return 0;
	}

	s2s_audio_lock();
	for (int e = 0; e < count; e++) {
		if (tbl[e].used && tbl[e].done != NULL) {
			tbl[e].done(channel, tbl[e].udata);
		}
		memset(&tbl[e], 0, sizeof(tbl[e]));
	}
	s2s_audio_unlock();
	return 1;
}

/* ── version ─────────────────────────────────────────────────────── */

const SDL_version *Mix_Linked_Version(void)
{
	static SDL_version v;

	SDL_MIXER_VERSION(&v);
	return &v;
}

/* ── music: stubbed (§6b) -- Doom plays silent, fully functional ─── */

static struct _Mix_Music {
	int dummy;
} music_sentinel;

Mix_Music *Mix_LoadMUS(const char *file)
{
	ARG_UNUSED(file);
	return &music_sentinel;
}

Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc)
{
	ARG_UNUSED(src);
	ARG_UNUSED(freesrc);
	return &music_sentinel;
}

void Mix_FreeMusic(Mix_Music *music)
{
	ARG_UNUSED(music);
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
	ARG_UNUSED(music);
	ARG_UNUSED(loops);
	return 0;
}

int Mix_HaltMusic(void)
{
	return 0;
}

int Mix_PlayingMusic(void)
{
	return 0;
}

int Mix_VolumeMusic(int volume)
{
	ARG_UNUSED(volume);
	return 0;
}

int Mix_SetMusicPosition(double position)
{
	ARG_UNUSED(position);
	return 0;
}

int Mix_SetMusicCMD(const char *command)
{
	ARG_UNUSED(command);
	return 0;
}
