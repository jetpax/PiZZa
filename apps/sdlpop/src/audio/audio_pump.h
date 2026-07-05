/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio pump -- freestanding C (HDMI-audio work order §2). One call
 * turns "whatever the consumer callback mixes at 44.1 kHz" into one
 * packed 192-frame IEC958 block at 48 kHz:
 *
 *   source callback (44.1 kHz S16 stereo; SDLPoP's audio_callback on
 *   target, synthetic sources in the host harness and the M2 test
 *   tone) -> resample_48k -> iec958_pack -> caller's DMA block.
 *
 * The pump carries the resampler state, so consecutive calls produce
 * a phase-continuous stream; block boundaries are inaudible. Pause
 * produces packed digital silence without touching the source (SDL
 * pause semantics: the device keeps running, the callback stops).
 *
 * No locking here: the Zephyr glue calls this from the feeder thread
 * only, holding the SDL audio lock while the source callback runs.
 */

#ifndef PIZZA_AUDIO_PUMP_H
#define PIZZA_AUDIO_PUMP_H

#include <stdint.h>

#include "iec958_pack.h"
#include "resample_48k.h"

#ifdef __cplusplus
extern "C" {
#endif

struct audio_pump {
	struct rs48 rs;
	rs48_source_fn source;
	void *source_user;
	int paused;
	int16_t mix[IEC958_BLOCK_FRAMES * 2];
};

void audio_pump_init(struct audio_pump *p, rs48_source_fn source,
		     void *source_user);

void audio_pump_set_paused(struct audio_pump *p, int paused);

/* Fill one DMA block: 384 IEC958 subframe words (1536 bytes). */
void audio_pump_fill_block(struct audio_pump *p, uint32_t *block);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_AUDIO_PUMP_H */
