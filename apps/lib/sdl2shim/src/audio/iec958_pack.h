/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * IEC 60958 subframe packer for the VC4 HDMI MAI path -- freestanding
 * C, no Zephyr headers, so it compiles and bit-exact-tests on host
 * (HDMI-audio work order §2, M0).
 *
 * The MAI FIFO consumes one 32-bit word per subframe in the ALSA
 * IEC958_SUBFRAME layout (the same convention alsa-lib's iec958
 * plugin feeds Linux vc4_hdmi, cross-checked against Circle's
 * CHDMISoundBaseDevice behavior on the same silicon):
 *
 *   [3:0]   preamble code -- B=0x8 (block start, left), M=0x2
 *           (left), W=0x4 (right). The B code must match the
 *           B_FRAME_IDENTIFIER field programmed into
 *           HDMI_AUDIO_PACKET_CONFIG (hdmi_audio_init.c).
 *   [27:4]  24-bit sample, two's complement; S16 input occupies the
 *           top 16 bits (sample << 8 within the 24-bit field).
 *   [28]    validity (0 = valid PCM)
 *   [29]    user data (unused, 0)
 *   [30]    channel status bit for this frame (both subframes of a
 *           frame carry the same status stream)
 *   [31]    parity: even over bits [31:4]; the preamble nibble is
 *           excluded (it is a code, not wire data -- the hardware
 *           regenerates the real sync preamble)
 *
 * Packing is block-based: one call packs exactly one 192-frame
 * channel-status block, so every DMA block starts at a B frame and a
 * precomputed silence block is phase-correct at any block boundary.
 */

#ifndef PIZZA_IEC958_PACK_H
#define PIZZA_IEC958_PACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One channel-status block: 192 frames, 2 subframes each. */
#define IEC958_BLOCK_FRAMES    192
#define IEC958_BLOCK_SUBFRAMES (IEC958_BLOCK_FRAMES * 2)
#define IEC958_BLOCK_BYTES     (IEC958_BLOCK_SUBFRAMES * 4)

/* Pack 192 frames of interleaved S16LE stereo (L,R per frame; 384
 * int16 values) into 384 IEC958 subframe words.
 */
void iec958_pack_block(const int16_t *pcm, uint32_t *out);

/* Pack a 192-frame block of digital silence (zero samples, valid
 * framing: preambles, channel status, parity).
 */
void iec958_silence_block(uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_IEC958_PACK_H */
