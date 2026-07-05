/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio pump -- see audio_pump.h.
 */

#include "audio_pump.h"

void audio_pump_init(struct audio_pump *p, rs48_source_fn source,
		     void *source_user)
{
	rs48_init(&p->rs);
	p->source = source;
	p->source_user = source_user;
	p->paused = 1;
}

void audio_pump_set_paused(struct audio_pump *p, int paused)
{
	p->paused = paused;
}

void audio_pump_fill_block(struct audio_pump *p, uint32_t *block)
{
	if (p->paused) {
		iec958_silence_block(block);
		return;
	}

	rs48_produce(&p->rs, p->mix, IEC958_BLOCK_FRAMES,
		     p->source, p->source_user);
	iec958_pack_block(p->mix, block);
}
