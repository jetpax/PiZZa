/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fixed-ratio 44100 -> 48000 polyphase resampler -- see resample_48k.h.
 *
 * Output frame stream position m maps to upsampled-grid index m*147;
 * with q = idx / 160 and r = idx % 160,
 *
 *   y[m] = sum_{k=0}^{TAPS-1} coef[r][k] * x[q - k]      (Q15 >> 15)
 *
 * The state walks that recurrence incrementally: `phase` is r with
 * the convention that a value >= 160 means the next input frame has
 * not been pulled yet.
 */

#include "resample_48k.h"

void rs48_init(struct rs48 *rs)
{
	rs->phase = RS48_L; /* force a pull before the first output */
	for (unsigned int k = 0; k < RS48_TAPS; k++) {
		rs->hist[k][0] = 0;
		rs->hist[k][1] = 0;
	}
}

static inline int16_t sat16(int64_t v)
{
	if (v > 32767) {
		return 32767;
	}
	if (v < -32768) {
		return -32768;
	}
	return (int16_t)v;
}

void rs48_produce(struct rs48 *rs, int16_t *out, unsigned int out_frames,
		  rs48_source_fn source, void *user)
{
	/* Work buffer: TAPS frames of history plus this call's pulls. */
	int16_t wb[RS48_TAPS + RS48_MAX_PULL(RS48_PRODUCE_MAX)][2];
	unsigned int r, need, cursor;

	/* Pass 1: count the input frames this call consumes. */
	r = rs->phase;
	need = 0;
	for (unsigned int m = 0; m < out_frames; m++) {
		while (r >= RS48_L) {
			r -= RS48_L;
			need++;
		}
		r += RS48_M;
	}

	for (unsigned int k = 0; k < RS48_TAPS; k++) {
		wb[k][0] = rs->hist[k][0];
		wb[k][1] = rs->hist[k][1];
	}
	if (need > 0) {
		source(user, &wb[RS48_TAPS][0], need);
	}

	/* Pass 2: same walk, filtering. cursor indexes the newest
	 * consumed frame in wb.
	 */
	r = rs->phase;
	cursor = RS48_TAPS - 1;
	for (unsigned int m = 0; m < out_frames; m++) {
		const int32_t *h;
		int64_t accl = 0, accr = 0;

		while (r >= RS48_L) {
			r -= RS48_L;
			cursor++;
		}

		h = rs48_coefs[r];
		for (unsigned int k = 0; k < RS48_TAPS; k++) {
			accl += (int64_t)h[k] * wb[cursor - k][0];
			accr += (int64_t)h[k] * wb[cursor - k][1];
		}
		out[2 * m]     = sat16(accl >> 15);
		out[2 * m + 1] = sat16(accr >> 15);

		r += RS48_M;
	}
	rs->phase = r;

	for (unsigned int k = 0; k < RS48_TAPS; k++) {
		rs->hist[k][0] = wb[cursor - (RS48_TAPS - 1) + k][0];
		rs->hist[k][1] = wb[cursor - (RS48_TAPS - 1) + k][1];
	}
}
