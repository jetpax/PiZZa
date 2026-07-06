/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cyclic DMA block-ring bookkeeping -- freestanding C (HDMI-audio
 * work order §2, M0). Pure cursor math over N equal blocks that the
 * DMA engine consumes in ring order; owns no memory and does no
 * locking (the Zephyr glue serializes ISR vs feeder with a spinlock).
 *
 * Protocol:
 *   - the engine is always inside block `play`; nobody writes it
 *   - the feeder fills blocks while aring_fill_next() returns >= 0
 *   - the per-block completion IRQ calls aring_on_block_done(); when
 *     the engine wraps into a block the feeder never refilled, the
 *     call reports an underrun and the caller stuffs the silence
 *     template into that block (underrun policy = repeat-silence:
 *     stale game audio is never replayed)
 */

#ifndef PIZZA_AUDIO_RING_H
#define PIZZA_AUDIO_RING_H

#ifdef __cplusplus
extern "C" {
#endif

struct aring {
	unsigned int nblocks;
	unsigned int play;      /* block the engine is currently in */
	unsigned int fill;      /* next block the feeder writes */
	unsigned int underruns; /* blocks replaced by silence */
	unsigned int started;   /* 0 until aring_start() */
};

/* Reset cursors. Blocks are considered unfilled. */
void aring_init(struct aring *r, unsigned int nblocks);

/* Returns the index the feeder should fill next, or -1 if the ring
 * is full (fill would collide with `play`). After actually filling
 * it, call aring_committed(idx): the commit is ignored if the ISR's
 * underrun path already moved the fill cursor past idx (the block
 * was silence-stuffed while the feeder was writing it -- one glitch
 * block, no stale-data hole).
 */
int aring_fill_next(const struct aring *r);
void aring_committed(struct aring *r, unsigned int idx);

/* Arm: the engine is about to start at block 0. Pre-fill at least
 * block 0 (normally the whole ring) first.
 */
void aring_start(struct aring *r);

/* A block finished playing; the engine advanced into the next one.
 * Returns 0 normally. Returns 1 on underrun -- the engine entered a
 * block that was never refilled; *stuff_idx names it and the caller
 * must overwrite it with silence NOW (the engine is reading it).
 */
int aring_on_block_done(struct aring *r, unsigned int *stuff_idx);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_AUDIO_RING_H */
