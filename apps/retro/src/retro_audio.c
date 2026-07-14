/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- audio bridge (audio_batch_cb -> HDMI MAI).
 *
 * libretro hands the frontend a core's mixed output through
 * audio_batch_cb; the proven HDMI-MAI backend (shim_audio_hdmi.c, linked
 * into this image by CONFIG_SDL2SHIM_AUDIO_HDMI) instead PULLS its samples
 * from an SDL-style callback on a feeder thread. This file is the SPSC
 * ring that joins the push side to the pull side:
 *
 *   core mixer -> glue s2s_audio_lr_pump -> audio_batch_cb  (PRODUCER,
 *     run thread) -> retro_audio_push -> [ring] -> drain_cb (CONSUMER,
 *     feeder thread) -> shim_audio_hdmi -> resample 44.1->48k -> IEC958
 *     -> DMA (DREQ 17) -> VC4 MAI -> HDMI audio packets
 *
 * Concurrency contract (desk-reviewed -- why no lock is needed):
 *   - Exactly one producer. audio_batch_cb runs only on the frontend run
 *     thread: retro_frontend_run_loaded's core.run() -> the core's
 *     retro_run -> s2s_audio_lr_pump -> this callback.
 *   - Exactly one consumer. drain_cb runs only on the shim_audio_hdmi
 *     feeder thread: feeder_fn -> audio_pump_fill_block -> game_source ->
 *     st.callback (held under the backend's SDL audio mutex, which our
 *     ring is independent of).
 *   - Single-producer / single-consumer, so free-running head/tail sample
 *     counters with release/acquire ordering are safe with no mutex. The
 *     board is UP (no SMP), where a compiler barrier alone would suffice;
 *     the atomics keep it correct if SMP is ever enabled.
 *   - The backend lives in THIS image, not a core llext, so there is no
 *     kernel-object donation concern (unlike shim_libretro.c). The core
 *     is byte-unchanged by this seam -- the wiring is frontend-internal.
 *
 * Rate is fixed 44.1 kHz S16 stereo: the sdl2shim-over-libretro glue pins
 * it, so the backend's 147:160 resampler is exact. A pure upstream
 * libretro core running at some other av_info sample_rate would push at
 * that rate and come out pitch-shifted -- variable-rate resample is M6.1.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "sdl2shim.h"
#include "retro_audio.h"

/* 16384 samples = 8192 stereo frames ~= 186 ms at 44.1 kHz. The producer
 * delivers one video frame's audio in a burst per retro_run (Doom: ~1260
 * frames every 28.6 ms) while the MAI feeder drains continuously; the run
 * loop paces off retro_audio_ring_fill() to hold the ring near a cushion
 * (RETRO_AUDIO_TARGET_FRAMES) so it never runs near-empty and the feeder
 * never underruns. The ring must hold the cushion + a full burst with
 * headroom. Power of two -> cheap mask wrap; even -> L/R channel alignment
 * survives the wrap since both cursors only ever move by even (whole-frame)
 * counts.
 */
#define RING_SAMPLES 16384u
#define RING_MASK    (RING_SAMPLES - 1u)

static int16_t ring[RING_SAMPLES];
static atomic_t r_head; /* total samples produced (free-running) */
static atomic_t r_tail; /* total samples consumed (free-running) */
static bool audio_ready;

/* Master gain in Q8 (256 = unity), applied as each sample enters the
 * ring -- the one point every core's mixed stream passes through, so it
 * is the RetroPiZZa-global volume. Boot value from Kconfig; `retro vol`
 * adjusts it live.
 */
static atomic_t gain_q8 = ATOMIC_INIT(256);

/* Consumer: the SDL audio callback the feeder thread pulls once per DMA
 * block. Copy up to `len` bytes from the ring, then pad any shortfall
 * with silence -- never stale audio, the same contract game_source in
 * shim_audio_hdmi.c already relies on.
 */
static void drain_cb(void *userdata, Uint8 *stream, int len)
{
	ARG_UNUSED(userdata);

	int16_t *out = (int16_t *)stream;
	size_t want = (size_t)len / sizeof(int16_t);
	uint64_t tail = (uint64_t)atomic_get(&r_tail);
	uint64_t head = (uint64_t)atomic_get(&r_head); /* acquire the producer's writes */
	size_t avail = (size_t)(head - tail);
	size_t take = MIN(want, avail);

	for (size_t i = 0; i < take; i++) {
		out[i] = ring[(size_t)(tail + i) & RING_MASK];
	}
	for (size_t i = take; i < want; i++) {
		out[i] = 0;
	}
	atomic_set(&r_tail, (atomic_val_t)(tail + take)); /* release the slots */
}

size_t retro_audio_push(const int16_t *data, size_t frames)
{
	if (!audio_ready || data == NULL || frames == 0) {
		return frames; /* consume-and-drop: nothing downstream */
	}

	size_t n = frames * 2u; /* interleaved stereo samples */
	uint64_t head = (uint64_t)atomic_get(&r_head);
	uint64_t tail = (uint64_t)atomic_get(&r_tail);
	size_t fill = (size_t)(head - tail);
	int32_t g = (int32_t)atomic_get(&gain_q8);

	/* Accept only up to the latency target (not ring capacity): the
	 * all-or-nothing reject is the glue producer's backpressure signal
	 * -- it retries the same chunk after a short sleep, so nothing is
	 * dropped, the MAI drain paces production, and audio lag stays
	 * bounded at ~TARGET frames (~35 ms) instead of the full ring.
	 */
	if (fill + n > RETRO_AUDIO_TARGET_FRAMES * 2u) {
		return 0;
	}

	for (size_t i = 0; i < n; i++) {
		int32_t s = ((int32_t)data[i] * g) >> 8;

		ring[(size_t)(head + i) & RING_MASK] =
			(int16_t)CLAMP(s, INT16_MIN, INT16_MAX);
	}
	atomic_set(&r_head, (atomic_val_t)(head + n)); /* publish after the writes */
	return frames;
}

void retro_audio_set_volume(unsigned int percent)
{
	if (percent > 200) {
		percent = 200;
	}
	atomic_set(&gain_q8, (atomic_val_t)(percent * 256U / 100U));
}

unsigned int retro_audio_get_volume(void)
{
	return (unsigned int)atomic_get(&gain_q8) * 100U / 256U;
}

int retro_audio_init(void)
{
	SDL_AudioSpec desired;

	retro_audio_set_volume(CONFIG_RETRO_AUDIO_GAIN_PERCENT);

	memset(&desired, 0, sizeof(desired));
	desired.freq = 44100;
	desired.format = AUDIO_S16SYS;
	desired.channels = 2;
	desired.samples = 1024; /* sizes desired.size only; the backend fixes its own blocks */
	desired.callback = drain_cb;
	desired.userdata = NULL;

	atomic_set(&r_head, 0);
	atomic_set(&r_tail, 0);

	if (s2s_audio_open(&desired, NULL) != 0) {
		printf("[retro-audio] HDMI audio unavailable: %s\n",
		       SDL_GetError());
		return -1;
	}

	audio_ready = true;
	s2s_audio_pause(0); /* start the feed; the ring stays silent until a core plays */
	printf("[retro-audio] HDMI MAI bridge open (44.1k S16 stereo)\n");
	return 0;
}
