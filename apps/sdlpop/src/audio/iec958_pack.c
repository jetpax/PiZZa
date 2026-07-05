/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * IEC 60958 subframe packer -- see iec958_pack.h for the word layout.
 */

#include "iec958_pack.h"

/* Consumer channel status, first 5 bytes (frames 0..39; the rest of
 * the 192-bit block is zero). Fixed for this path's one output mode:
 *
 *   byte 0: 0x04 -- consumer, PCM, copy permitted, no pre-emphasis
 *   byte 1: 0x00 -- category: general
 *   byte 2: 0x00 -- source/channel not indicated
 *   byte 3: 0x02 -- sampling frequency 48 kHz
 *   byte 4: 0x02 -- word length 16 bits (20-bit max frame)
 *
 * Status bits are consumed LSB-first: frame n carries bit (n % 8) of
 * byte (n / 8).
 */
static const uint8_t iec958_status[5] = { 0x04, 0x00, 0x00, 0x02, 0x02 };

#define IEC958_PRE_B 0x8u
#define IEC958_PRE_M 0x2u
#define IEC958_PRE_W 0x4u

static inline uint32_t parity32(uint32_t w)
{
	w ^= w >> 16;
	w ^= w >> 8;
	w ^= w >> 4;
	w ^= w >> 2;
	w ^= w >> 1;
	return w & 1u;
}

static inline uint32_t pack_subframe(int16_t s, unsigned int frame,
				     unsigned int right)
{
	/* S16 -> 24-bit field (<< 8), into bits [27:4] (<< 4 more). */
	uint32_t w = (uint32_t)(uint16_t)s << 12;

	if (frame < 8u * sizeof(iec958_status) &&
	    ((iec958_status[frame >> 3] >> (frame & 7u)) & 1u)) {
		w |= 1u << 30;
	}

	/* Even parity over bits [31:4]; preamble bits [3:0] are still
	 * zero here and excluded by construction.
	 */
	if (parity32(w)) {
		w |= 1u << 31;
	}

	if (right) {
		w |= IEC958_PRE_W;
	} else {
		w |= (frame == 0u) ? IEC958_PRE_B : IEC958_PRE_M;
	}

	return w;
}

void iec958_pack_block(const int16_t *pcm, uint32_t *out)
{
	for (unsigned int f = 0; f < IEC958_BLOCK_FRAMES; f++) {
		out[2 * f]     = pack_subframe(pcm[2 * f], f, 0);
		out[2 * f + 1] = pack_subframe(pcm[2 * f + 1], f, 1);
	}
}

void iec958_silence_block(uint32_t *out)
{
	for (unsigned int f = 0; f < IEC958_BLOCK_FRAMES; f++) {
		out[2 * f]     = pack_subframe(0, f, 0);
		out[2 * f + 1] = pack_subframe(0, f, 1);
	}
}
