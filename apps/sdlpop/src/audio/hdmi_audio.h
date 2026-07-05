/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * VC4 HDMI MAI audio hardware programming (HDMI-audio work order §2,
 * M2 surface). Fixed-purpose: 48 kHz 2-channel IEC958 PCM on the
 * firmware-established HDMI output of BCM2710-class silicon. The
 * video side (HDMI block scheduler, PHY, pixel clock, HVS) is
 * firmware territory and is treated as READ ONLY here; this module
 * only adds the audio plumbing on top: MAI config/format/thresholds,
 * the audio infoframe via packet RAM, and N/CTS clock regeneration.
 *
 * Register facts (names, offsets, bit positions) are from the two
 * proven references on this silicon -- Linux vc4_hdmi.c/vc4_regs.h
 * and Circle's CHDMISoundBaseDevice -- reimplemented clean-room
 * (work-order R4).
 */

#ifndef PIZZA_HDMI_AUDIO_H
#define PIZZA_HDMI_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map the HD / HDMI / clock-manager register blocks, verify the
 * firmware brought HDMI up in HDMI mode with packet RAM enabled
 * (DVI mode carries no audio -- work-order R2), recover the HSM and
 * pixel clock rates, and program the full MAI + infoframe + N/CTS
 * state. Idempotent. Returns 0, or:
 *   -ENODEV  packet RAM disabled (DVI mode / no HDMI display)
 *   -EIO     clock recovery failed or infoframe handshake timed out
 */
int hdmi_audio_hw_init(void);

/* Start/stop audio transmission. Enable AFTER the DMA feed is
 * running (Circle's proven order); disable before stopping DMA.
 */
void hdmi_audio_hw_enable(void);
void hdmi_audio_hw_disable(void);

/* ARM-physical address of the MAI data FIFO register -- the DMA
 * destination (the DMA driver translates to the VC bus address).
 */
uintptr_t hdmi_audio_mai_data_phys(void);

/* Snapshot for the `pop audio` diagnostic command and the M2 report:
 * recovered clocks, programmed dividers, and the live MAI_CTL word
 * (FULL/EMPTY/ERROR bits tell the DREQ-pacing story).
 */
struct hdmi_audio_hw_debug {
	uint32_t hsm_hz;
	uint32_t pixel_hz;
	uint32_t smp_n;
	uint32_t smp_m;
	uint32_t crp_n;
	uint32_t cts;
	uint32_t mai_ctl;
};

void hdmi_audio_hw_get_debug(struct hdmi_audio_hw_debug *out);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_HDMI_AUDIO_H */
