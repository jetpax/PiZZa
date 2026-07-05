/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host harness for the HDMI-audio core (work order M0 gate, plus the
 * M3 sim tranche). Compiles the freestanding src/audio modules on the
 * host and verifies:
 *
 *   1. IEC958 framing, bit-exactly, with an independent checker that
 *      re-derives every subframe from the layout rules (preamble
 *      sequence B/M/W, 48 kHz consumer-PCM channel status, even
 *      parity over bits [31:4], payload placement).
 *   2. The 147:160 resampler against reference signals: DC exactness,
 *      1 kHz sine THD+N bound, band-center amplitude flatness.
 *   3. Ring bookkeeping incl. the underrun path: prefill protocol,
 *      steady state, buffer drain, sustained underrun (silence
 *      stuffing), recovery.
 *   4. The full pump path (M3 sim tranche): a synthetic two-sine
 *      "game mix" pulled through resample + pack across many blocks,
 *      framing re-verified on the whole stream, payload unpacked and
 *      sine-fit to prove block boundaries are phase-continuous.
 *
 * Golden dumps for the runtime report land in out/.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_pump.h"
#include "iec958_pack.h"
#include "resample_48k.h"
#include "ring.h"

static int g_failures;

#define CHECK(cond, ...)                                                       \
	do {                                                                   \
		if (!(cond)) {                                                 \
			g_failures++;                                          \
			printf("FAIL %s:%d: ", __func__, __LINE__);            \
			printf(__VA_ARGS__);                                   \
			printf("\n");                                          \
		}                                                              \
	} while (0)

/* ── independent IEC958 subframe checker ─────────────────────────
 * Everything here is re-derived from the layout contract, not from
 * the packer's code: B=0x8/M=0x2/W=0x4 preamble codes, S16 payload in
 * bits [27:12] of the 24-bit field, consumer 48 kHz status bytes
 * {04,00,00,02,02} LSB-first over frames 0..39, even parity over bits
 * [31:4].
 */
static const uint8_t ref_status[5] = { 0x04, 0x00, 0x00, 0x02, 0x02 };

static int ref_parity_even(uint32_t w)
{
	return (__builtin_popcount(w & 0xFFFFFFF0u) & 1) == 0;
}

static void check_subframe(uint32_t w, int16_t expect_sample,
			   unsigned int frame, unsigned int right,
			   const char *tag)
{
	uint32_t pre = w & 0xFu;
	uint32_t expect_pre = right ? 0x4u : (frame == 0 ? 0x8u : 0x2u);
	int16_t sample = (int16_t)((w << 4) >> 16);
	uint32_t vbit = (w >> 28) & 1u;
	uint32_t ubit = (w >> 29) & 1u;
	uint32_t cbit = (w >> 30) & 1u;
	uint32_t expect_c = frame < 40
		? (uint32_t)((ref_status[frame >> 3] >> (frame & 7u)) & 1u)
		: 0u;

	CHECK(pre == expect_pre, "%s f%u%s preamble %x != %x",
	      tag, frame, right ? "R" : "L", pre, expect_pre);
	CHECK(sample == expect_sample, "%s f%u%s sample %d != %d",
	      tag, frame, right ? "R" : "L", sample, expect_sample);
	CHECK((w & (0xFFu << 4)) == 0, "%s f%u low pad bits set", tag, frame);
	CHECK(vbit == 0, "%s f%u validity set", tag, frame);
	CHECK(ubit == 0, "%s f%u user bit set", tag, frame);
	CHECK(cbit == expect_c, "%s f%u status bit %u != %u",
	      tag, frame, cbit, expect_c);
	CHECK(ref_parity_even(w), "%s f%u%s parity odd",
	      tag, frame, right ? "R" : "L");
}

static void check_stream(const uint32_t *words, const int16_t *pcm,
			 unsigned int nblocks, const char *tag)
{
	for (unsigned int b = 0; b < nblocks; b++) {
		for (unsigned int f = 0; f < IEC958_BLOCK_FRAMES; f++) {
			unsigned int i = b * IEC958_BLOCK_SUBFRAMES + 2 * f;

			check_subframe(words[i], pcm[i], f, 0, tag);
			check_subframe(words[i + 1], pcm[i + 1], f, 1, tag);
		}
	}
}

/* ── 1. packer framing, golden dump ──────────────────────────────── */

static void test_iec958(void)
{
	enum { NBLOCKS = 4 };
	static int16_t pcm[NBLOCKS * IEC958_BLOCK_FRAMES * 2];
	static uint32_t out[NBLOCKS * IEC958_BLOCK_SUBFRAMES];
	FILE *fp;

	/* Known stream: a ramp crossing zero plus edge values. */
	for (unsigned int i = 0; i < sizeof(pcm) / sizeof(pcm[0]); i++) {
		pcm[i] = (int16_t)(-32768 + (int)(i * 43));
	}
	pcm[0] = 0;
	pcm[1] = 32767;
	pcm[2] = -32768;
	pcm[3] = 1;

	for (unsigned int b = 0; b < NBLOCKS; b++) {
		iec958_pack_block(&pcm[b * IEC958_BLOCK_FRAMES * 2],
				  &out[b * IEC958_BLOCK_SUBFRAMES]);
	}

	check_stream(out, pcm, NBLOCKS, "pack");

	/* Silence block must equal packing literal zeros. */
	{
		static const int16_t zeros[IEC958_BLOCK_FRAMES * 2];
		uint32_t sil[IEC958_BLOCK_SUBFRAMES];
		uint32_t ref[IEC958_BLOCK_SUBFRAMES];

		iec958_silence_block(sil);
		iec958_pack_block(zeros, ref);
		CHECK(memcmp(sil, ref, sizeof(sil)) == 0,
		      "silence block != packed zeros");
		check_stream(sil, zeros, 1, "silence");
	}

	fp = fopen("out/iec958_golden.bin", "wb");
	if (fp != NULL) {
		fwrite(out, sizeof(out), 1, fp);
		fclose(fp);
	}

	printf("iec958: %u subframes verified (preamble/status/parity/payload), golden dump out/iec958_golden.bin\n",
	       NBLOCKS * IEC958_BLOCK_SUBFRAMES);
}

/* ── 2. resampler ───────────────────────────────────────────────── */

struct sine_src {
	double freq;
	double amp;
	uint64_t n;
};

static void sine_pull(void *user, int16_t *dst, unsigned int frames)
{
	struct sine_src *s = user;

	for (unsigned int i = 0; i < frames; i++) {
		double v = s->amp * sin(2.0 * M_PI * s->freq *
					(double)s->n / 44100.0);
		int16_t q = (int16_t)lrint(v);

		dst[2 * i] = q;
		dst[2 * i + 1] = q;
		s->n++;
	}
}

struct dc_src {
	int16_t value;
};

static void dc_pull(void *user, int16_t *dst, unsigned int frames)
{
	struct dc_src *s = user;

	for (unsigned int i = 0; i < frames; i++) {
		dst[2 * i] = s->value;
		dst[2 * i + 1] = s->value;
	}
}

/* Least-squares fit of a*sin + b*cos at freq on ch0 of interleaved
 * stereo; returns signal power and residual (THD+N) power.
 */
static void sine_fit(const int16_t *x, unsigned int frames, double freq,
		     double fs, double *sig_pow, double *res_pow)
{
	double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0;

	for (unsigned int i = 0; i < frames; i++) {
		double w = 2.0 * M_PI * freq * (double)i / fs;
		double s = sin(w), c = cos(w);
		double v = x[2 * i];

		ss += s * s; sc += s * c; cc += c * c;
		xs += v * s; xc += v * c;
	}

	double det = ss * cc - sc * sc;
	double a = (xs * cc - xc * sc) / det;
	double b = (xc * ss - xs * sc) / det;
	double sp = 0, rp = 0;

	for (unsigned int i = 0; i < frames; i++) {
		double w = 2.0 * M_PI * freq * (double)i / fs;
		double fit = a * sin(w) + b * cos(w);
		double v = x[2 * i];

		sp += fit * fit;
		rp += (v - fit) * (v - fit);
	}

	*sig_pow = sp / frames;
	*res_pow = rp / frames;
}

static double run_sine_thd(double freq, double amp, double *amp_ratio)
{
	enum { WARM_BLOCKS = 4, MEAS_BLOCKS = 250 };
	static int16_t out[(WARM_BLOCKS + MEAS_BLOCKS) * 192 * 2];
	struct sine_src src = { .freq = freq, .amp = amp };
	struct rs48 rs;
	double sig, res;

	rs48_init(&rs);
	for (unsigned int b = 0; b < WARM_BLOCKS + MEAS_BLOCKS; b++) {
		rs48_produce(&rs, &out[b * 192 * 2], 192, sine_pull, &src);
	}

	sine_fit(&out[WARM_BLOCKS * 192 * 2], MEAS_BLOCKS * 192,
		 freq, 48000.0, &sig, &res);

	if (amp_ratio != NULL) {
		*amp_ratio = sqrt(2.0 * sig) / amp;
	}

	return 10.0 * log10(res / sig);
}

static void test_resampler(void)
{
	/* DC passes bit-exactly (per-phase rows sum to 1<<15). */
	{
		struct dc_src src = { .value = 1234 };
		struct rs48 rs;
		int16_t out[192 * 2];
		int exact = 1;

		rs48_init(&rs);
		for (unsigned int b = 0; b < 10; b++) {
			rs48_produce(&rs, out, 192, dc_pull, &src);
			if (b == 0) {
				continue; /* warmup: zero history ramp-in */
			}
			for (unsigned int i = 0; i < 192 * 2; i++) {
				if (out[i] != 1234) {
					exact = 0;
				}
			}
		}
		CHECK(exact, "DC 1234 not bit-exact after warmup");
		printf("resampler: DC bit-exact\n");
	}

	{
		double ratio;
		double thd = run_sine_thd(1000.0, 0.5 * 32767.0, &ratio);

		CHECK(thd < -70.0, "1 kHz THD+N %.1f dB > -70 dB", thd);
		CHECK(fabs(ratio - 1.0) < 0.01,
		      "1 kHz amplitude ratio %.4f off by >1%%", ratio);
		printf("resampler: 1 kHz THD+N %.1f dB, amplitude ratio %.5f\n",
		       thd, ratio);
	}

	{
		double ratio;
		double thd = run_sine_thd(10000.0, 0.5 * 32767.0, &ratio);

		CHECK(thd < -70.0, "10 kHz THD+N %.1f dB > -70 dB", thd);
		CHECK(fabs(ratio - 1.0) < 0.02,
		      "10 kHz amplitude ratio %.4f off by >2%%", ratio);
		printf("resampler: 10 kHz THD+N %.1f dB, amplitude ratio %.5f\n",
		       thd, ratio);
	}
}

/* ── 3. ring / underrun ─────────────────────────────────────────── */

static void test_ring(void)
{
	enum { NB = 8 };
	struct aring r;
	unsigned int stuff = 999;
	int idx;

	aring_init(&r, NB);

	/* Prefill claims each block exactly once, then reports full. */
	for (unsigned int i = 0; i < NB; i++) {
		idx = aring_fill_next(&r);
		CHECK(idx == (int)i, "prefill slot %d != %u", idx, i);
		aring_committed(&r, (unsigned int)idx);
	}
	CHECK(aring_fill_next(&r) == -1, "prefill: ring not full after %u", NB);

	aring_start(&r);
	CHECK(aring_fill_next(&r) == -1, "full ring writable after start");

	/* Steady state: each completion frees exactly the played block. */
	for (unsigned int i = 0; i < 3 * NB; i++) {
		CHECK(aring_on_block_done(&r, &stuff) == 0,
		      "unexpected underrun at done %u", i);
		idx = aring_fill_next(&r);
		CHECK(idx == (int)(i % NB), "refill slot %d != %u", idx, i % NB);
		aring_committed(&r, (unsigned int)idx);
		CHECK(aring_fill_next(&r) == -1, "ring not full after refill");
	}

	/* Feeder stalls: NB blocks of buffered audio drain without
	 * underrun, then every wrap stuffs silence.
	 */
	for (unsigned int i = 0; i < NB - 1; i++) {
		CHECK(aring_on_block_done(&r, &stuff) == 0,
		      "drain underrun at %u", i);
	}
	CHECK(r.underruns == 0, "underruns %u != 0 before drain end",
	      r.underruns);

	for (unsigned int i = 0; i < 3; i++) {
		unsigned int expect = r.play + 1 == NB ? 0 : r.play + 1;

		CHECK(aring_on_block_done(&r, &stuff) == 1,
		      "missing underrun at starved done %u", i);
		CHECK(stuff == expect, "stuff idx %u != %u", stuff, expect);
	}
	CHECK(r.underruns == 3, "underruns %u != 3", r.underruns);

	/* Recovery: feeder returns, may fill everything but the block
	 * the engine is in, then steady state resumes.
	 */
	{
		unsigned int filled = 0;

		while ((idx = aring_fill_next(&r)) >= 0) {
			CHECK(idx != (int)r.play, "fill on playing block");
			aring_committed(&r, (unsigned int)idx);
			filled++;
		}
		CHECK(filled == NB - 1, "recovery filled %u != %u",
		      filled, NB - 1);
	}
	CHECK(aring_on_block_done(&r, &stuff) == 0, "underrun after recovery");

	/* Commit-overtake race: the feeder claims a slot and stalls
	 * mid-write; the engine drains the ring and the ISR underrun
	 * path silence-stuffs that very slot. The feeder's late commit
	 * must be a no-op (no unfilled block sneaks into the ring).
	 */
	{
		unsigned int claimed, expect_fill;

		idx = aring_fill_next(&r);
		CHECK(idx >= 0, "no slot free for overtake test");
		claimed = (unsigned int)idx;

		for (unsigned int i = 0; i < NB - 2; i++) {
			CHECK(aring_on_block_done(&r, &stuff) == 0,
			      "early underrun in overtake drain at %u", i);
		}
		CHECK(aring_on_block_done(&r, &stuff) == 1,
		      "missing underrun on claimed slot");
		CHECK(stuff == claimed, "stuffed %u != claimed %u",
		      stuff, claimed);
		expect_fill = r.fill;

		aring_committed(&r, claimed);
		CHECK(r.fill == expect_fill, "late commit moved fill cursor");

		while ((idx = aring_fill_next(&r)) >= 0) {
			aring_committed(&r, (unsigned int)idx);
		}
		CHECK(aring_on_block_done(&r, &stuff) == 0,
		      "underrun after overtake recovery");
	}

	printf("ring: prefill/steady/drain/underrun/recovery/commit-overtake verified (underruns=%u)\n",
	       r.underruns);
}

/* ── 4. full pump path (M3 sim tranche) ─────────────────────────── */

/* Joint least-squares fit of two sines (4 basis functions), so the
 * finite-window leakage between the tones -- which dominates a
 * sequential single-tone fit at these amplitudes -- cancels exactly.
 * Returns signal power (of the fit) and residual power.
 */
static void sine_fit2(const int16_t *x, unsigned int frames, double f1,
		      double f2, double fs, double *sig_pow, double *res_pow)
{
	double a[4][4] = { { 0 } };
	double v[4] = { 0 };
	double coef[4];
	double basis[4];

	for (unsigned int i = 0; i < frames; i++) {
		double w1 = 2.0 * M_PI * f1 * (double)i / fs;
		double w2 = 2.0 * M_PI * f2 * (double)i / fs;
		double xv = x[2 * i];

		basis[0] = sin(w1); basis[1] = cos(w1);
		basis[2] = sin(w2); basis[3] = cos(w2);
		for (int r = 0; r < 4; r++) {
			for (int c = 0; c < 4; c++) {
				a[r][c] += basis[r] * basis[c];
			}
			v[r] += basis[r] * xv;
		}
	}

	/* Gaussian elimination with partial pivoting. */
	for (int col = 0; col < 4; col++) {
		int piv = col;

		for (int r = col + 1; r < 4; r++) {
			if (fabs(a[r][col]) > fabs(a[piv][col])) {
				piv = r;
			}
		}
		for (int c = 0; c < 4; c++) {
			double t = a[col][c];

			a[col][c] = a[piv][c];
			a[piv][c] = t;
		}
		{
			double t = v[col];

			v[col] = v[piv];
			v[piv] = t;
		}
		for (int r = col + 1; r < 4; r++) {
			double m = a[r][col] / a[col][col];

			for (int c = col; c < 4; c++) {
				a[r][c] -= m * a[col][c];
			}
			v[r] -= m * v[col];
		}
	}
	for (int r = 3; r >= 0; r--) {
		double s = v[r];

		for (int c = r + 1; c < 4; c++) {
			s -= a[r][c] * coef[c];
		}
		coef[r] = s / a[r][r];
	}

	{
		double sp = 0, rp = 0;

		for (unsigned int i = 0; i < frames; i++) {
			double w1 = 2.0 * M_PI * f1 * (double)i / fs;
			double w2 = 2.0 * M_PI * f2 * (double)i / fs;
			double fit = coef[0] * sin(w1) + coef[1] * cos(w1) +
				     coef[2] * sin(w2) + coef[3] * cos(w2);
			double xv = x[2 * i];

			sp += fit * fit;
			rp += (xv - fit) * (xv - fit);
		}
		*sig_pow = sp / frames;
		*res_pow = rp / frames;
	}
}

struct mix_src {
	uint64_t n;
};

/* Synthetic "game mix": two tones at different levels, mimicking
 * OPL3 music + a digi effect summed in SDLPoP's audio_callback.
 */
static void mix_pull(void *user, int16_t *dst, unsigned int frames)
{
	struct mix_src *s = user;

	for (unsigned int i = 0; i < frames; i++) {
		double t = (double)s->n / 44100.0;
		double v = 8000.0 * sin(2.0 * M_PI * 440.0 * t) +
			   4000.0 * sin(2.0 * M_PI * 2093.0 * t);

		dst[2 * i] = (int16_t)lrint(v);
		dst[2 * i + 1] = (int16_t)lrint(v);
		s->n++;
	}
}

static void test_pump(void)
{
	enum { NBLOCKS = 200, WARM = 4 };
	static uint32_t stream[NBLOCKS * IEC958_BLOCK_SUBFRAMES];
	static int16_t unpacked[NBLOCKS * IEC958_BLOCK_FRAMES * 2];
	struct mix_src src = { 0 };
	struct audio_pump pump;
	FILE *fp;

	audio_pump_init(&pump, mix_pull, &src);

	/* Paused pump emits the exact silence template. */
	{
		uint32_t sil[IEC958_BLOCK_SUBFRAMES];
		uint32_t ref[IEC958_BLOCK_SUBFRAMES];

		audio_pump_fill_block(&pump, sil);
		iec958_silence_block(ref);
		CHECK(memcmp(sil, ref, sizeof(sil)) == 0,
		      "paused pump block != silence template");
	}

	audio_pump_set_paused(&pump, 0);
	for (unsigned int b = 0; b < NBLOCKS; b++) {
		audio_pump_fill_block(&pump,
				      &stream[b * IEC958_BLOCK_SUBFRAMES]);
	}

	/* Framing invariants must hold across the whole stream; the
	 * payload check needs the expected PCM, so unpack first and
	 * verify payload-vs-unpacked (structure), then sine-fit the
	 * unpacked audio (content).
	 */
	for (unsigned int i = 0; i < NBLOCKS * IEC958_BLOCK_SUBFRAMES; i++) {
		unpacked[i] = (int16_t)((stream[i] << 4) >> 16);
	}
	check_stream(stream, unpacked, NBLOCKS, "pump");

	/* Joint two-tone fit: remove both tones, residual is THD+N of
	 * the full mix->resample->pack->unpack path. Phase continuity
	 * across block boundaries is what keeps this small.
	 */
	{
		double sig, res;
		unsigned int off = WARM * IEC958_BLOCK_FRAMES;
		unsigned int n = (NBLOCKS - WARM) * IEC958_BLOCK_FRAMES;

		sine_fit2(&unpacked[off * 2], n, 440.0, 2093.0, 48000.0,
			  &sig, &res);

		double thd = 10.0 * log10(res / sig);

		CHECK(thd < -70.0, "pump two-tone residual %.1f dB > -70 dB",
		      thd);
		printf("pump: %u blocks phase-continuous, two-tone residual %.1f dB\n",
		       NBLOCKS, thd);
	}

	fp = fopen("out/pump_stream.bin", "wb");
	if (fp != NULL) {
		fwrite(stream, sizeof(stream), 1, fp);
		fclose(fp);
	}
}

int main(void)
{
	test_iec958();
	test_resampler();
	test_ring();
	test_pump();

	if (g_failures == 0) {
		printf("ALL PASS\n");
		return 0;
	}
	printf("%d FAILURE(S)\n", g_failures);
	return 1;
}
