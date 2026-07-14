/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host harness -- run a clean sine through the RetroPiZZa audio bridge in
 * simulation, to isolate the new transport (the SPSC ring + the 44.1->48k
 * resample) from the OPL/game source and from HW timing.
 *
 * The path mirrored here (everything up to the DMA/MAI, which is HW-only):
 *
 *   sine @44.1k -> retro_audio ring (push bursts / drain chunks, an exact
 *   replica of apps/retro/src/retro_audio.c) -> rs48 resample 147:160 ->
 *   48k -> WAV + THD.
 *
 * Proves: ring pass-through is bit-exact (no reorder / drop / channel swap)
 * and the resampled sine is low-distortion. It CANNOT prove (HW only):
 * underruns from real MAI-clock vs run-loop timing, or the OPL synth source.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "resample_48k.h" /* the real resampler */

/* ── exact replica of the retro_audio SPSC ring ──────────────────────── */
#define RING_SAMPLES 16384u
#define RING_MASK    (RING_SAMPLES - 1u)
static int16_t ring[RING_SAMPLES];
static uint64_t r_head, r_tail;

static void ring_push(const int16_t *data, size_t frames)
{
	size_t n = frames * 2u;
	size_t space = RING_SAMPLES - (size_t)(r_head - r_tail);

	if (n > space) {
		return; /* drop-on-full: a dropped burst is a gap, not distortion */
	}
	for (size_t i = 0; i < n; i++) {
		ring[(size_t)(r_head + i) & RING_MASK] = data[i];
	}
	r_head += n;
}

static size_t ring_drain(int16_t *out, size_t frames)
{
	size_t want = frames * 2u;
	size_t avail = (size_t)(r_head - r_tail);
	size_t take = want < avail ? want : avail;

	for (size_t i = 0; i < take; i++) {
		out[i] = ring[(size_t)(r_tail + i) & RING_MASK];
	}
	for (size_t i = take; i < want; i++) {
		out[i] = 0;
	}
	r_tail += take;
	return take / 2u;
}

/* ── resample source: a cursor over the drained 44.1k PCM ────────────── */
struct pcm_src {
	const int16_t *buf;
	size_t pos;
	size_t frames;
};

static void pcm_pull(void *user, int16_t *dst, unsigned int frames)
{
	struct pcm_src *s = user;

	for (unsigned int i = 0; i < frames; i++) {
		size_t p = s->pos < s->frames ? s->pos : s->frames - 1;

		dst[i * 2] = s->buf[p * 2];
		dst[i * 2 + 1] = s->buf[p * 2 + 1];
		s->pos++;
	}
}

/* ── WAV writer (16-bit stereo) ──────────────────────────────────────── */
static void write_wav(const char *path, const int16_t *pcm, size_t frames,
		      uint32_t rate)
{
	FILE *f = fopen(path, "wb");
	uint32_t data_bytes, byte_rate;
	uint8_t h[44];

	if (!f) {
		return;
	}
	data_bytes = (uint32_t)frames * 4u;
	byte_rate = rate * 4u;
	memcpy(h, "RIFF", 4);
	*(uint32_t *)(h + 4) = 36 + data_bytes;
	memcpy(h + 8, "WAVEfmt ", 8);
	*(uint32_t *)(h + 16) = 16;
	*(uint16_t *)(h + 20) = 1;
	*(uint16_t *)(h + 22) = 2;
	*(uint32_t *)(h + 24) = rate;
	*(uint32_t *)(h + 28) = byte_rate;
	*(uint16_t *)(h + 32) = 4;
	*(uint16_t *)(h + 34) = 16;
	memcpy(h + 36, "data", 4);
	*(uint32_t *)(h + 40) = data_bytes;
	fwrite(h, 1, 44, f);
	fwrite(pcm, 2, frames * 2, f);
	fclose(f);
}

/* Least-squares single-sine fit -> THD+N in dB (left channel). */
static double thd_db(const int16_t *x, size_t frames, double freq, double rate)
{
	double w = 2.0 * M_PI * freq / rate;
	double sc = 0, ss = 0, cc = 0, xs = 0, xc = 0, px = 0;
	double det, a, b, sig, res;

	for (size_t i = 0; i < frames; i++) {
		double s = sin(w * (double)i), c = cos(w * (double)i);
		double v = x[i * 2];

		sc += s * c; ss += s * s; cc += c * c;
		xs += v * s; xc += v * c; px += v * v;
	}
	det = ss * cc - sc * sc;
	a = (xs * cc - xc * sc) / det;
	b = (xc * ss - xs * sc) / det;
	sig = a * a * ss + b * b * cc + 2 * a * b * sc;
	res = px - sig;
	if (res < 1e-9) {
		res = 1e-9;
	}
	return 10.0 * log10(res / sig);
}

int main(void)
{
	const double rate_in = 44100.0, freq = 1000.0;
	const size_t total_in = (size_t)(rate_in * 2); /* 2 s */
	const size_t burst = 1260;   /* one Doom frame (44100/35) */
	const size_t chunk = 178;    /* ~ one 192-frame IEC958 block's input */
	const size_t cushion = 1536; /* RETRO_AUDIO_TARGET_FRAMES */

	static int16_t sine[44100 * 2 * 2];
	static int16_t drained[44100 * 2 * 2 + 8192];
	static int16_t out48[48000 * 2 * 2 + 8192];
	static int16_t silence[1536 * 2];
	struct pcm_src src;
	struct rs48 rs;
	size_t in_pos = 0, out_pos = 0, o48 = 0, mism = 0, underruns = 0;
	double thd;

	for (size_t i = 0; i < total_in; i++) {
		int16_t v = (int16_t)(0.5 * 32767.0 *
				      sin(2.0 * M_PI * freq * (double)i / rate_in));
		sine[i * 2] = v;
		sine[i * 2 + 1] = v;
	}

	/* Prefill the pacing cushion, then per Doom frame push one burst and
	 * drain a matching amount in small chunks (bursty producer, steady
	 * consumer) -- the real data flow.
	 */
	memset(silence, 0, sizeof(silence));
	ring_push(silence, cushion);
	while (in_pos + burst <= total_in) {
		ring_push(&sine[in_pos * 2], burst);
		in_pos += burst;
		/* Drain exactly `burst` per cycle -- the real 147:160 resampler
		 * consumes exactly one push worth per frame, so push and drain
		 * rates match. Chunk only sets the per-call granularity.
		 */
		for (size_t c = 0; c < burst; ) {
			size_t this_chunk = (burst - c) < chunk ? (burst - c) : chunk;

			if (ring_drain(&drained[out_pos * 2], this_chunk) < this_chunk) {
				underruns++;
			}
			out_pos += this_chunk;
			c += this_chunk;
		}
	}

	/* Bit-exactness: past the cushion, drained must equal the input sine. */
	for (size_t i = 0; i + cushion < out_pos && i + burst < total_in; i++) {
		if (drained[(i + cushion) * 2] != sine[i * 2] ||
		    drained[(i + cushion) * 2 + 1] != sine[i * 2 + 1]) {
			mism++;
		}
	}

	/* Resample the drained 44.1k sine to 48k through the REAL rs48. */
	src.buf = drained;
	src.pos = cushion; /* skip the silence cushion */
	src.frames = out_pos;
	rs48_init(&rs);
	while (src.pos + 178 < src.frames &&
	       (o48 + 192) * 2 < sizeof(out48) / sizeof(out48[0])) {
		rs48_produce(&rs, &out48[o48 * 2], 192, pcm_pull, &src);
		o48 += 192;
	}

	thd = thd_db(&out48[4800 * 2], o48 > 14400 ? 9600 : o48 / 2, freq, 48000.0);

	write_wav("out/retro_sine_in_44k.wav", sine, total_in, 44100);
	write_wav("out/retro_sine_drained_44k.wav", drained, out_pos, 44100);
	write_wav("out/retro_sine_out_48k.wav", out48, o48, 48000);

	printf("== RetroPiZZa audio-bridge sine sim ==\n");
	printf("ring pass-through : %zu sample mismatches (0 = bit-exact)\n", mism);
	printf("ring underruns    : %zu (0 = ring never starved in sim)\n",
	       underruns);
	printf("resampled THD+N   : %.1f dB (< -60 = clean sine)\n", thd);
	printf("WAVs: out/retro_sine_{in_44k,drained_44k,out_48k}.wav\n");
	return (mism == 0 && thd < -60.0) ? 0 : 1;
}
