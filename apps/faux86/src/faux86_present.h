/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Frame present seam for the PiZZa Faux86 port. The chosen backend
 * (semihost PPM / bcm2835_fb display / none) implements these; the Faux86
 * host FrameBufferInterface::blit() calls them. Pixels are 32bpp XRGB
 * (0x00RRGGBB), which is what Faux86 emits when built with DEPTH=32.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int s2s_present_init(int width, int height);
void s2s_present_frame(const void *xrgb_pixels, int pitch);

#ifdef __cplusplus
}
#endif
