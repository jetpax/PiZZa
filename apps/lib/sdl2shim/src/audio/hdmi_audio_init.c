/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * VC4 HDMI MAI audio hardware programming -- see hdmi_audio.h.
 *
 * Block layout on the VC bus (ARM window base derived from the DMA
 * node's address, so BCM2835/BCM2710 both resolve):
 *
 *   HD    +0x808000  MAI control/format/threshold/data/sample-rate
 *   HDMI  +0x902000  MAI config/map, audio packet config, packet
 *                    RAM, CRP (N/CTS), TX PHY control
 *   CM    +0x101000  clock manager: HSM divider, PLLH (pixel clock)
 *
 * Programming sequence and register facts follow Linux vc4_hdmi.c
 * (gen1 path) cross-checked against Circle CHDMISoundBaseDevice;
 * both are GPL references read for facts, reimplemented (R4).
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "hdmi_audio.h"

LOG_MODULE_REGISTER(sdlpop_hdmi_audio, CONFIG_SDL2SHIM_LOG_LEVEL);

#define PERIPH_BASE (DT_REG_ADDR(DT_NODELABEL(dma)) & 0xFF000000UL)
#define HD_PHYS     (PERIPH_BASE + 0x808000UL)
#define HDMI_PHYS   (PERIPH_BASE + 0x902000UL)
#define CM_PHYS     (PERIPH_BASE + 0x101000UL)

/* HD block */
#define HD_MAI_CTL  0x14
#define HD_MAI_THR  0x18
#define HD_MAI_FMT  0x1c
#define HD_MAI_DATA 0x20
#define HD_MAI_SMP  0x2c

#define MAI_CTL_RESET   BIT(0)
#define MAI_CTL_ERRORF  BIT(1)
#define MAI_CTL_ERRORE  BIT(2)
#define MAI_CTL_ENABLE  BIT(3)
#define MAI_CTL_CHNUM(n) ((uint32_t)(n) << 4)
#define MAI_CTL_FLUSH   BIT(9)
#define MAI_CTL_WHOLSMP BIT(12)
#define MAI_CTL_CHALIGN BIT(13)
#define MAI_CTL_DLATE   BIT(15)

/* MAI_FMT: audio format [23:16] (PCM = 2), sample rate code [15:8]
 * (48 kHz = 9 in the 8000/11025/…/192000 table).
 */
#define MAI_FMT_PCM_48K ((2u << 16) | (9u << 8))

/* MAI_THR fields: PANICHIGH [29:24], PANICLOW [21:16],
 * DREQHIGH [13:8], DREQLOW [5:0]. Values are the Linux gen1 set;
 * Circle uses 16 across all four -- fallback if DREQ pacing
 * misbehaves at bring-up.
 */
#define MAI_THR_VAL ((8u << 24) | (8u << 16) | (6u << 8) | 8u)

/* MAI_SMP: N [31:8], M-1 [7:0] -- MAI bit-clock N/M against HSM. */

/* HDMI block */
#define HDMI_MAI_CHANNEL_MAP     0x090
#define HDMI_MAI_CONFIG          0x094
#define HDMI_AUDIO_PACKET_CONFIG 0x09c
#define HDMI_RAM_PACKET_CONFIG   0x0a0
#define HDMI_RAM_PACKET_STATUS   0x0a4
#define HDMI_CRP_CFG             0x0a8
#define HDMI_CTS_0               0x0ac
#define HDMI_CTS_1               0x0b0
#define HDMI_TX_PHY_CTL_0        0x2c4
#define HDMI_RAM_PACKET_START    0x400
#define HDMI_PACKET_STRIDE       0x24

#define MAI_CONFIG_BIT_REVERSE    BIT(26)
#define MAI_CONFIG_FORMAT_REVERSE BIT(27)

#define AUDIO_PACKET_ZERO_DATA_ON_SAMPLE_FLAT       BIT(29)
#define AUDIO_PACKET_ZERO_DATA_ON_INACTIVE_CHANNELS BIT(24)
#define AUDIO_PACKET_B_FRAME_ID(x) ((uint32_t)(x) << 10)

#define RAM_PACKET_ENABLE BIT(16)
/* Audio infoframe = HDMI packet type 0x84 -> packet id 4. */
#define RAM_PACKET_AUDIO  BIT(4)

#define CRP_CFG_EXTERNAL_CTS_EN BIT(24)

#define TX_PHY_RNG_PWRDN BIT(25)

/* Clock manager: HSM divider (12.12 fixed point, 4 integer + 8
 * fractional bits populated) off PLLD; PLLH drives the pixel clock
 * through a fixed /10 on the PIX output. PLLD and the crystal are
 * firmware-fixed rates on this platform.
 */
#define CM_HSMDIV      0x08c
#define A2W_PLLH_CTRL  0x1960
#define A2W_PLLH_FRAC  0x1a60
#define A2W_PLLH_ANA1  0x1074
#define PLLH_FB_PREDIV BIT(11)
#define PLLH_PIX_DIV   10u
#define PLLD_HZ        500000000u
#define XOSC_HZ        19200000u

#define SAMPLE_RATE_HZ 48000u

static struct {
	mem_addr_t hd;
	mem_addr_t hdmi;
	mem_addr_t cm;
	int mapped;
	struct hdmi_audio_hw_debug dbg;
} hw;

static uint32_t hsm_clock_hz(void)
{
	uint32_t div = sys_read32(hw.cm + CM_HSMDIV);

	div >>= 12 - 8;
	div &= (1u << (4 + 8)) - 1u;
	if (div == 0) {
		return 0;
	}

	return (uint32_t)(((uint64_t)PLLD_HZ << 8) / div);
}

static uint32_t pixel_clock_hz(void)
{
	uint32_t ctrl = sys_read32(hw.cm + A2W_PLLH_CTRL);
	uint64_t frac = sys_read32(hw.cm + A2W_PLLH_FRAC) & GENMASK(19, 0);
	uint64_t ndiv = ctrl & GENMASK(9, 0);
	uint32_t pdiv = (ctrl >> 12) & 0x7u;
	uint64_t rate;

	if (sys_read32(hw.cm + A2W_PLLH_ANA1) & PLLH_FB_PREDIV) {
		ndiv *= 2;
		frac *= 2;
	}
	if (pdiv == 0) {
		return 0;
	}

	rate = (uint64_t)XOSC_HZ * ((ndiv << 20) + frac);
	rate /= pdiv;
	rate >>= 20;

	return (uint32_t)(rate / PLLH_PIX_DIV);
}

/* Best rational n/d ~= num/den with n <= max_n, d <= max_d, by
 * continued-fraction convergents.
 */
static void best_rational(uint32_t num, uint32_t den, uint32_t max_n,
			  uint32_t max_d, uint32_t *out_n, uint32_t *out_d)
{
	uint32_t n = num, d = den;
	uint32_t n0 = 0, d0 = 1, n1 = 1, d1 = 0;

	while (d != 0) {
		uint32_t a = n / d;
		uint32_t rem = n % d;
		uint64_t n2 = (uint64_t)n0 + (uint64_t)a * n1;
		uint64_t d2 = (uint64_t)d0 + (uint64_t)a * d1;

		if (n2 > max_n || d2 > max_d) {
			break;
		}
		n0 = n1; d0 = d1;
		n1 = (uint32_t)n2; d1 = (uint32_t)d2;
		n = d; d = rem;
	}

	if (d1 == 0) {
		n1 = max_n;
		d1 = 1;
	}
	*out_n = n1;
	*out_d = d1;
}

static int wait_packet_status(uint32_t mask, bool set)
{
	for (int i = 0; i < 100; i++) {
		uint32_t st = sys_read32(hw.hdmi + HDMI_RAM_PACKET_STATUS);

		if (((st & mask) != 0) == set) {
			return 0;
		}
		k_msleep(1);
	}
	return -EIO;
}

/* CEA-861 audio infoframe via packet RAM: type 0x84, version 1,
 * length 10, payload byte 1 = 0x01 (2 channels, coding "refer to
 * stream header"), all other payload zero, checksum 0x70 so the
 * header+payload byte sum is 0 mod 256. Packet RAM packs 7 infoframe
 * bytes per 8-byte register pair: word0 = header (3 bytes), word1 =
 * checksum + first 3 payload bytes.
 */
static int write_audio_infoframe(void)
{
	mem_addr_t pkt = hw.hdmi + HDMI_RAM_PACKET_START +
			 4 * HDMI_PACKET_STRIDE;
	uint32_t cfg;
	int err;

	cfg = sys_read32(hw.hdmi + HDMI_RAM_PACKET_CONFIG);
	sys_write32(cfg & ~RAM_PACKET_AUDIO, hw.hdmi + HDMI_RAM_PACKET_CONFIG);
	err = wait_packet_status(RAM_PACKET_AUDIO, false);
	if (err != 0) {
		return err;
	}

	sys_write32(0x000A0184u, pkt);
	sys_write32(0x00000170u, pkt + 4);
	for (uint32_t off = 8; off < HDMI_PACKET_STRIDE; off += 4) {
		sys_write32(0, pkt + off);
	}

	cfg = sys_read32(hw.hdmi + HDMI_RAM_PACKET_CONFIG);
	sys_write32(cfg | RAM_PACKET_AUDIO, hw.hdmi + HDMI_RAM_PACKET_CONFIG);
	return wait_packet_status(RAM_PACKET_AUDIO, true);
}

static void stop_audio_infoframe(void)
{
	uint32_t cfg = sys_read32(hw.hdmi + HDMI_RAM_PACKET_CONFIG);

	sys_write32(cfg & ~RAM_PACKET_AUDIO, hw.hdmi + HDMI_RAM_PACKET_CONFIG);
	(void)wait_packet_status(RAM_PACKET_AUDIO, false);
}

int hdmi_audio_hw_init(void)
{
	uint32_t hsm, pixel, n, m, crp_n, cts;
	int err;

	if (!hw.mapped) {
		device_map(&hw.hd, HD_PHYS, 0x100,
			   K_MEM_CACHE_NONE | K_MEM_PERM_RW);
		device_map(&hw.hdmi, HDMI_PHYS, 0x600,
			   K_MEM_CACHE_NONE | K_MEM_PERM_RW);
		device_map(&hw.cm, CM_PHYS, 0x2000,
			   K_MEM_CACHE_NONE | K_MEM_PERM_RW);
		hw.mapped = 1;
	}

	/* Firmware leaves packet RAM enabled only when the display is
	 * driven in HDMI mode (hdmi_drive=2); in DVI mode there is no
	 * audio transport at all (R2).
	 */
	if (!(sys_read32(hw.hdmi + HDMI_RAM_PACKET_CONFIG) &
	      RAM_PACKET_ENABLE)) {
		LOG_ERR("packet RAM disabled: DVI mode or no HDMI display");
		return -ENODEV;
	}

	hsm = hsm_clock_hz();
	pixel = pixel_clock_hz();
	if (hsm == 0 || pixel == 0) {
		LOG_ERR("clock recovery failed (hsm=%u pixel=%u)", hsm, pixel);
		return -EIO;
	}

	/* MAI sample clock: best N/M with N <= 24 bits, M <= 256. */
	best_rational(hsm, SAMPLE_RATE_HZ, GENMASK(23, 0), 256, &n, &m);

	/* Clock regeneration for 48 kHz: standard N = 128*fs/1000;
	 * CTS = pixel_clock * N / (128 * fs). COUPLING (R3): CTS is
	 * derived from the CURRENT pixel clock -- any future HVS/video
	 * work that reprograms the pixel clock must re-run
	 * hdmi_audio_hw_init() or audio regeneration silently breaks.
	 */
	crp_n = 128u * SAMPLE_RATE_HZ / 1000u;
	cts = (uint32_t)(((uint64_t)pixel * crp_n) /
			 (128u * SAMPLE_RATE_HZ));

	sys_write32(MAI_CTL_RESET | MAI_CTL_FLUSH | MAI_CTL_DLATE |
		    MAI_CTL_ERRORE | MAI_CTL_ERRORF, hw.hd + HD_MAI_CTL);
	sys_write32((n << 8) | (m - 1u), hw.hd + HD_MAI_SMP);
	sys_write32(MAI_FMT_PCM_48K, hw.hd + HD_MAI_FMT);
	sys_write32(MAI_THR_VAL, hw.hd + HD_MAI_THR);

	/* Channel mask 0b11 (2 channels), subframes bit/format-reversed
	 * to match the ALSA IEC958_SUBFRAME layout the packer emits;
	 * channel map: ch0 -> slot 0, ch1 -> slot 1.
	 */
	sys_write32(MAI_CONFIG_BIT_REVERSE | MAI_CONFIG_FORMAT_REVERSE | 0x3u,
		    hw.hdmi + HDMI_MAI_CONFIG);
	sys_write32(0x8u, hw.hdmi + HDMI_MAI_CHANNEL_MAP);

	/* B-frame identifier 0x8 must match the packer's B preamble
	 * code (iec958_pack.c).
	 */
	sys_write32(AUDIO_PACKET_ZERO_DATA_ON_SAMPLE_FLAT |
		    AUDIO_PACKET_ZERO_DATA_ON_INACTIVE_CHANNELS |
		    AUDIO_PACKET_B_FRAME_ID(0x8) | 0x3u,
		    hw.hdmi + HDMI_AUDIO_PACKET_CONFIG);

	sys_write32(CRP_CFG_EXTERNAL_CTS_EN | crp_n, hw.hdmi + HDMI_CRP_CFG);
	sys_write32(cts, hw.hdmi + HDMI_CTS_0);
	sys_write32(cts, hw.hdmi + HDMI_CTS_1);

	err = write_audio_infoframe();
	if (err != 0) {
		LOG_ERR("audio infoframe handshake timed out");
		return err;
	}

	hw.dbg.hsm_hz = hsm;
	hw.dbg.pixel_hz = pixel;
	hw.dbg.smp_n = n;
	hw.dbg.smp_m = m;
	hw.dbg.crp_n = crp_n;
	hw.dbg.cts = cts;

	LOG_INF("MAI up: hsm %u Hz, pixel %u Hz, SMP %u/%u, N %u, CTS %u",
		hsm, pixel, n, m, crp_n, cts);
	return 0;
}

void hdmi_audio_hw_enable(void)
{
	uint32_t phy = sys_read32(hw.hdmi + HDMI_TX_PHY_CTL_0);

	sys_write32(phy & ~TX_PHY_RNG_PWRDN, hw.hdmi + HDMI_TX_PHY_CTL_0);
	sys_write32(MAI_CTL_CHNUM(2) | MAI_CTL_WHOLSMP | MAI_CTL_CHALIGN |
		    MAI_CTL_ENABLE, hw.hd + HD_MAI_CTL);
}

void hdmi_audio_hw_disable(void)
{
	uint32_t phy;

	sys_write32(MAI_CTL_DLATE | MAI_CTL_ERRORE | MAI_CTL_ERRORF,
		    hw.hd + HD_MAI_CTL);

	phy = sys_read32(hw.hdmi + HDMI_TX_PHY_CTL_0);
	sys_write32(phy | TX_PHY_RNG_PWRDN, hw.hdmi + HDMI_TX_PHY_CTL_0);

	stop_audio_infoframe();

	sys_write32(MAI_CTL_RESET, hw.hd + HD_MAI_CTL);
	sys_write32(MAI_CTL_ERRORF, hw.hd + HD_MAI_CTL);
	sys_write32(MAI_CTL_FLUSH, hw.hd + HD_MAI_CTL);
}

uintptr_t hdmi_audio_mai_data_phys(void)
{
	return HD_PHYS + HD_MAI_DATA;
}

void hdmi_audio_hw_get_debug(struct hdmi_audio_hw_debug *out)
{
	*out = hw.dbg;
	out->mai_ctl = hw.mapped ? sys_read32(hw.hd + HD_MAI_CTL) : 0;
}
