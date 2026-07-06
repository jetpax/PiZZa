/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cyclic DMA block-ring bookkeeping -- see ring.h.
 */

#include "ring.h"

void aring_init(struct aring *r, unsigned int nblocks)
{
	r->nblocks = nblocks;
	r->play = 0;
	r->fill = 0;
	r->underruns = 0;
	r->started = 0;
}

int aring_fill_next(const struct aring *r)
{
	/* Before start the engine isn't consuming: allow filling every
	 * block except a wrap back onto block 0 (nblocks - 1 of them,
	 * plus block 0 itself first -- i.e. all of them exactly once).
	 */
	if (!r->started) {
		if (r->fill == 0 && r->play == 1) {
			return -1; /* wrapped: all blocks filled */
		}
		return (int)r->fill;
	}

	if (r->fill == r->play) {
		return -1;
	}
	return (int)r->fill;
}

void aring_committed(struct aring *r, unsigned int idx)
{
	if (r->fill != idx) {
		return; /* ISR underrun path overtook this commit */
	}
	r->fill = (r->fill + 1) % r->nblocks;
	if (!r->started && r->fill == 0) {
		/* Pre-start full marker: park play off-index so
		 * aring_fill_next() reports full until aring_start().
		 */
		r->play = 1;
	}
}

void aring_start(struct aring *r)
{
	/* fill stays where prefill left it: if the whole ring was
	 * pre-filled, fill == play == 0 reads as "full" until the
	 * first block completes, which is exactly right.
	 */
	r->play = 0;
	r->started = 1;
}

int aring_on_block_done(struct aring *r, unsigned int *stuff_idx)
{
	r->play = (r->play + 1) % r->nblocks;

	if (r->play == r->fill) {
		/* The feeder never got to this block: it still holds
		 * already-played data. Silence it and move the fill
		 * cursor past it so the feeder resumes behind the
		 * engine.
		 */
		*stuff_idx = r->play;
		r->fill = (r->play + 1) % r->nblocks;
		r->underruns++;
		return 1;
	}

	return 0;
}
