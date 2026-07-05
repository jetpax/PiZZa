/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- HDMI audio backend (CONFIG_SDLPOP_AUDIO_HDMI).
 * Implements the §5a audio function table for real: SDL_OpenAudio
 * succeeds and the game's callback-mixed 44.1 kHz stream comes out
 * of the TV speakers (HDMI-audio work order, M2/M3 glue).
 *
 * Data path (everything below the pump is this file):
 *
 *   game audio_callback (44.1 kHz S16 stereo)
 *     -> audio_pump (resample 147:160 + IEC958 pack)   [feeder thread]
 *     -> 8-block cyclic ring, 192 frames (4 ms) per block
 *     -> BCM2835 DMA channel, DREQ 17 (HDMI), dest = MAI data FIFO
 *     -> VC4 MAI -> HDMI audio sample packets
 *
 * The per-block DMA completion IRQ drives ring bookkeeping; the
 * feeder thread wakes on a semaphore and refills every free block,
 * calling the game's callback under the SDL audio lock. Underruns
 * play packed silence, never stale audio (ring.c policy). Cache: the
 * DMA engine does not snoop the A53 caches, so every block write is
 * flushed before the engine can reach it; the driver flushes the
 * initial prefill at dma_start().
 *
 * The M2 test tone (CONFIG_SDLPOP_AUDIO_HDMI_TEST_TONE) reuses this
 * whole path with a synthetic 1 kHz source at boot, so the tone
 * validates DMA pacing, MAI setup AND the resampler/packer on
 * hardware before the game ever runs.
 */

#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pop_shim.h"
#include "audio/audio_pump.h"
#include "audio/hdmi_audio.h"
#include "audio/iec958_pack.h"
#include "audio/ring.h"

LOG_MODULE_REGISTER(pop_audio, CONFIG_SDLPOP_LOG_LEVEL);

#define AUDIO_NB     8 /* ring depth: 8 x 4 ms = 32 ms of buffer */
#define HDMI_DREQ    17
#define FEEDER_STACK 16384

static const struct device *const dma_dev =
	DEVICE_DT_GET(DT_NODELABEL(dma));

/* Blocks are 1536 bytes (64-byte multiple), 64-byte aligned: no
 * cache line is shared between blocks or with other data.
 */
static uint32_t ring_buf[AUDIO_NB][IEC958_BLOCK_SUBFRAMES] __aligned(64);
static uint32_t silence_blk[IEC958_BLOCK_SUBFRAMES];

static struct {
	struct audio_pump pump;
	struct aring ring;
	struct k_spinlock lock; /* ring cursors: ISR vs feeder */
	int dma_chan;
	bool opened;
	bool tone_mode;
	bool paused;
	volatile bool closing;
	void (*callback)(void *userdata, Uint8 *stream, int len);
	void *userdata;
	uint32_t blocks_played;
	uint32_t dma_errors;
	volatile uint32_t starve; /* M2 acceptance: skip N feeder wakes */
} st = { .dma_chan = -1 };

static K_SEM_DEFINE(refill_sem, 0, 1);
static K_MUTEX_DEFINE(sdl_audio_mutex);
static K_THREAD_STACK_DEFINE(feeder_stack, FEEDER_STACK);
static struct k_thread feeder_thread;
static k_tid_t feeder_tid;

/* ── source adapters ─────────────────────────────────────────────── */

/* Game side: SDL callback contract is (userdata, byte buffer, byte
 * length); the pump asks in frames of S16 stereo.
 */
static void game_source(void *user, int16_t *dst, unsigned int frames)
{
	ARG_UNUSED(user);
	memset(dst, 0, frames * 2 * sizeof(int16_t));
	if (st.callback != NULL) {
		st.callback(st.userdata, (Uint8 *)dst,
			    (int)(frames * 2 * sizeof(int16_t)));
	}
}

#ifdef CONFIG_SDLPOP_AUDIO_HDMI_TEST_TONE
/* 1 kHz at 44.1 kHz input rate: 441 samples = exactly 10 cycles, so
 * looping the table is phase-continuous. Runs through the full
 * resample + pack path. -12 dBFS.
 */
static int16_t tone_table[441];
static unsigned int tone_pos;

static void tone_source(void *user, int16_t *dst, unsigned int frames)
{
	ARG_UNUSED(user);
	for (unsigned int i = 0; i < frames; i++) {
		int16_t s = tone_table[tone_pos];

		tone_pos = (tone_pos + 1) % ARRAY_SIZE(tone_table);
		dst[2 * i] = s;
		dst[2 * i + 1] = s;
	}
}
#endif

/* ── DMA completion (ISR) ────────────────────────────────────────── */

static void dma_block_done(const struct device *dev, void *user_data,
			   uint32_t channel, int status)
{
	k_spinlock_key_t key;
	unsigned int stuff;

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	ARG_UNUSED(channel);

	if (status < 0) {
		st.dma_errors++;
	}

	key = k_spin_lock(&st.lock);
	st.blocks_played++;
	if (aring_on_block_done(&st.ring, &stuff)) {
		/* Engine wrapped into a never-refilled block: it must
		 * play silence, not stale audio. It is being read
		 * right now -- a partial glitch beats a full replay.
		 */
		memcpy(ring_buf[stuff], silence_blk, IEC958_BLOCK_BYTES);
		sys_cache_data_flush_range(ring_buf[stuff],
					   IEC958_BLOCK_BYTES);
	}
	k_spin_unlock(&st.lock, key);

	k_sem_give(&refill_sem);
}

/* ── feeder thread ───────────────────────────────────────────────── */

static void feeder_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		k_sem_take(&refill_sem, K_FOREVER);
		if (st.closing) {
			return;
		}
		if (st.starve > 0) {
			/* Underrun-recovery demo (`pop audio starve`):
			 * ignore this wake so the engine drains the
			 * ring; the ISR stuffs silence, then normal
			 * refill resumes.
			 */
			st.starve--;
			continue;
		}

		while (true) {
			k_spinlock_key_t key = k_spin_lock(&st.lock);
			int idx = aring_fill_next(&st.ring);

			k_spin_unlock(&st.lock, key);
			if (idx < 0) {
				break;
			}

			k_mutex_lock(&sdl_audio_mutex, K_FOREVER);
			audio_pump_fill_block(&st.pump, ring_buf[idx]);
			k_mutex_unlock(&sdl_audio_mutex);

			sys_cache_data_flush_range(ring_buf[idx],
						   IEC958_BLOCK_BYTES);

			key = k_spin_lock(&st.lock);
			aring_committed(&st.ring, (unsigned int)idx);
			k_spin_unlock(&st.lock, key);
		}
	}
}

/* ── open/close plumbing shared by game and test tone ────────────── */

static int hdmi_open_common(rs48_source_fn source, void *source_user)
{
	struct dma_block_config blocks[AUDIO_NB];
	struct dma_config cfg;
	int err, idx;

	if (!device_is_ready(dma_dev)) {
		pop_set_error("audio: DMA controller not ready");
		return -1;
	}

	err = hdmi_audio_hw_init();
	if (err != 0) {
		pop_set_error("audio: HDMI audio unavailable (%d)", err);
		return -1;
	}

	st.dma_chan = dma_request_channel(dma_dev, NULL);
	if (st.dma_chan < 0) {
		pop_set_error("audio: no free DMA channel");
		return -1;
	}

	audio_pump_init(&st.pump, source, source_user);
	iec958_silence_block(silence_blk);
	aring_init(&st.ring, AUDIO_NB);

	/* Prefill the whole ring (pump starts paused -> packed
	 * silence); dma_start() flushes these to DRAM.
	 */
	while ((idx = aring_fill_next(&st.ring)) >= 0) {
		audio_pump_fill_block(&st.pump, ring_buf[idx]);
		aring_committed(&st.ring, (unsigned int)idx);
	}

	memset(blocks, 0, sizeof(blocks));
	for (unsigned int i = 0; i < AUDIO_NB; i++) {
		blocks[i].source_address = (uintptr_t)ring_buf[i];
		blocks[i].dest_address = hdmi_audio_mai_data_phys();
		blocks[i].block_size = IEC958_BLOCK_BYTES;
		blocks[i].source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		blocks[i].dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		blocks[i].next_block =
			(i + 1 < AUDIO_NB) ? &blocks[i + 1] : NULL;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	cfg.dma_slot = HDMI_DREQ;
	cfg.block_count = AUDIO_NB;
	cfg.head_block = &blocks[0];
	cfg.cyclic = 1;
	cfg.dma_callback = dma_block_done;

	err = dma_config(dma_dev, (uint32_t)st.dma_chan, &cfg);
	if (err != 0) {
		pop_set_error("audio: dma_config failed (%d)", err);
		goto fail_release;
	}

	feeder_tid = k_thread_create(&feeder_thread, feeder_stack,
				     K_THREAD_STACK_SIZEOF(feeder_stack),
				     feeder_fn, NULL, NULL, NULL,
				     K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(feeder_tid, "hdmi_audio");

	aring_start(&st.ring);
	err = dma_start(dma_dev, (uint32_t)st.dma_chan);
	if (err != 0) {
		pop_set_error("audio: dma_start failed (%d)", err);
		k_thread_abort(feeder_tid);
		goto fail_release;
	}

	/* Circle-proven order: the DREQ-paced feed is running before
	 * the MAI consumer turns on.
	 */
	hdmi_audio_hw_enable();

	st.paused = true;
	st.opened = true;
	LOG_INF("HDMI audio open: 48 kHz stereo, %u x %u B ring, DMA ch %d",
		AUDIO_NB, IEC958_BLOCK_BYTES, st.dma_chan);
	return 0;

fail_release:
	dma_release_channel(dma_dev, (uint32_t)st.dma_chan);
	st.dma_chan = -1;
	return -1;
}

/* ── §5a function table ──────────────────────────────────────────── */

int pop_audio_open(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	if (st.opened) {
		pop_set_error("audio: already open%s",
			      st.tone_mode ? " (test tone)" : "");
		return -1;
	}
	if (desired == NULL || desired->freq != 44100 ||
	    desired->format != AUDIO_S16SYS || desired->channels != 2 ||
	    desired->callback == NULL) {
		pop_set_error("audio: backend is fixed 44100/S16/2ch/callback");
		return -1;
	}

	st.callback = desired->callback;
	st.userdata = desired->userdata;

	if (hdmi_open_common(game_source, NULL) != 0) {
		st.callback = NULL;
		return -1;
	}

	desired->silence = 0;
	desired->size = (Uint32)desired->samples * 2 * sizeof(int16_t);
	if (obtained != NULL) {
		*obtained = *desired;
	}
	return 0;
}

void pop_audio_close(void)
{
	if (!st.opened) {
		return;
	}

	hdmi_audio_hw_disable();
	dma_stop(dma_dev, (uint32_t)st.dma_chan);

	/* Cooperative exit so the feeder never dies holding the SDL
	 * audio mutex mid-fill.
	 */
	st.closing = true;
	k_sem_give(&refill_sem);
	k_thread_join(feeder_tid, K_FOREVER);
	st.closing = false;

	dma_release_channel(dma_dev, (uint32_t)st.dma_chan);
	st.dma_chan = -1;
	st.opened = false;
	st.tone_mode = false;
	st.callback = NULL;
}

void pop_audio_pause(int pause_on)
{
	if (!st.opened) {
		return;
	}
	k_mutex_lock(&sdl_audio_mutex, K_FOREVER);
	audio_pump_set_paused(&st.pump, pause_on);
	st.paused = (pause_on != 0);
	k_mutex_unlock(&sdl_audio_mutex);
}

void pop_audio_lock(void)
{
	k_mutex_lock(&sdl_audio_mutex, K_FOREVER);
}

void pop_audio_unlock(void)
{
	k_mutex_unlock(&sdl_audio_mutex);
}

SDL_AudioStatus pop_audio_status(void)
{
	if (!st.opened) {
		return SDL_AUDIO_STOPPED;
	}
	return st.paused ? SDL_AUDIO_PAUSED : SDL_AUDIO_PLAYING;
}

/* ── diagnostics (`pop audio` shell command) ─────────────────────── */

void pop_audio_hdmi_starve(unsigned int blocks)
{
	st.starve = blocks;
}

void pop_audio_hdmi_get_stats(struct pop_audio_hdmi_stats *out)
{
	k_spinlock_key_t key = k_spin_lock(&st.lock);

	out->opened = st.opened;
	out->tone_mode = st.tone_mode;
	out->paused = st.paused;
	out->blocks_played = st.blocks_played;
	out->underruns = st.ring.underruns;
	out->dma_errors = st.dma_errors;
	out->dma_chan = st.dma_chan;
	k_spin_unlock(&st.lock, key);
}

/* ── M2 test tone ────────────────────────────────────────────────── */

#ifdef CONFIG_SDLPOP_AUDIO_HDMI_TEST_TONE

#include <math.h>

static int tone_start(void)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(tone_table); i++) {
		tone_table[i] = (int16_t)(8192.0f *
			sinf(2.0f * 3.14159265f * 1000.0f * (float)i /
			     44100.0f));
	}

	st.tone_mode = true;
	if (hdmi_open_common(tone_source, NULL) != 0) {
		st.tone_mode = false;
		LOG_ERR("test tone failed to start: %s", SDL_GetError());
		return 0; /* keep booting; `pop audio` shows the state */
	}

	audio_pump_set_paused(&st.pump, 0);
	st.paused = false;
	st.opened = true;
	LOG_INF("M2 test tone: 1 kHz -12 dBFS via full resample/pack path");
	return 0;
}

SYS_INIT(tone_start, APPLICATION, 99);

#endif /* CONFIG_SDLPOP_AUDIO_HDMI_TEST_TONE */
