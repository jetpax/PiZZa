#!/usr/bin/env python3
"""Generate the Q15 polyphase FIR table for the fixed 44100 -> 48000
resampler (src/audio/resample_48k.c).

Rational ratio 44100:48000 = 147:160. The prototype filter is a
Kaiser-windowed sinc designed at the L=160 upsampled rate
(7.056 MHz): 32 taps per phase, 160 phases, 5120 taps total.

Design targets (documented in the M0 report):
  - stopband attenuation ~80 dB (Kaiser beta 7.857)
  - cutoff centered at 19.5 kHz -> stopband edge ~23.7 kHz, below the
    48 kHz output Nyquist, so images of the 44.1 kHz baseband fold in
    at <= -80 dB
  - per-phase coefficient rows normalized to sum exactly 32768, so DC
    passes bit-exactly and the filter adds no level shift

The output header is committed to the repo; the build does not run
this script. Regenerate with:
  python3 tools/gen_resample_coefs.py > src/audio/resample_48k_coefs.h
"""

import math

L = 160          # interpolation (phases)
M = 147          # decimation
K = 32           # taps per phase
FS_IN = 44100.0
FC_HZ = 19500.0  # -6 dB cutoff of the prototype lowpass
BETA = 7.857     # Kaiser beta for ~80 dB stopband

N = L * K


def bessel_i0(x):
    """Modified Bessel function of the first kind, order 0 (series)."""
    s, t = 1.0, 1.0
    for k in range(1, 64):
        t *= (x / (2.0 * k)) ** 2
        s += t
        if t < 1e-21 * s:
            break
    return s


def kaiser(n, length, beta):
    r = 2.0 * n / (length - 1) - 1.0
    return bessel_i0(beta * math.sqrt(max(0.0, 1.0 - r * r))) / bessel_i0(beta)


def sinc(x):
    if x == 0.0:
        return 1.0
    return math.sin(math.pi * x) / (math.pi * x)


def main():
    fc = FC_HZ / (FS_IN * L)  # cutoff as fraction of the upsampled rate
    center = (N - 1) / 2.0

    proto = [2.0 * fc * sinc(2.0 * fc * (i - center)) * kaiser(i, N, BETA)
             for i in range(N)]

    # Polyphase reorganization: coef[r][k] = proto[k*L + r], then
    # normalize each row's sum to exactly 1<<15 in Q15.
    rows = []
    for r in range(L):
        row_f = [proto[k * L + r] for k in range(K)]
        scale = 32768.0 / sum(row_f)
        row_q = [round(c * scale) for c in row_f]
        err = 32768 - sum(row_q)
        imax = max(range(K), key=lambda k: abs(row_q[k]))
        row_q[imax] += err
        assert sum(row_q) == 32768
        rows.append(row_q)

    print("/*")
    print(" * SPDX-License-Identifier: Apache-2.0")
    print(" *")
    print(" * Q15 polyphase FIR table for the fixed 147:160")
    print(" * (44100 -> 48000) resampler. GENERATED FILE -- do not edit;")
    print(" * regenerate with tools/gen_resample_coefs.py.")
    print(" *")
    print(f" * {K} taps/phase, {L} phases, Kaiser beta {BETA},")
    print(f" * cutoff {FC_HZ:.0f} Hz, per-row sums exactly 32768.")
    print(" */")
    print()
    print("#ifndef PIZZA_RESAMPLE_48K_COEFS_H")
    print("#define PIZZA_RESAMPLE_48K_COEFS_H")
    print()
    print("#include <stdint.h>")
    print()
    print(f"#define RS48_L {L}")
    print(f"#define RS48_M {M}")
    print(f"#define RS48_TAPS {K}")
    print()
    print(f"static const int32_t rs48_coefs[{L}][{K}] = {{")
    for r, row in enumerate(rows):
        body = ", ".join(str(c) for c in row)
        print(f"\t{{ {body} }},")
    print("};")
    print()
    print("#endif /* PIZZA_RESAMPLE_48K_COEFS_H */")


if __name__ == "__main__":
    main()
