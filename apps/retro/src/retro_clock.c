/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- ARM clock bump (rpi_zero_2w).
 *
 * The VideoCore firmware idles the Cortex-A53 at arm_freq_min (~600 MHz)
 * unless something requests the turbo clock; Zephyr runs no cpufreq
 * governor. A Doom-class core plus the inline audio mix (SFX + OPL synth)
 * needs more than one 600 MHz core-second per second (measured ~137% --
 * game ~19 ms + pump ~21 ms per 28.6 ms frame), so pin the A53 at its
 * rated 1.0 GHz at boot, the same SET_CLOCK_RATE bump the Jet / wipeout /
 * DOSBox / faux86 ports use.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>

#include "retro_clock.h"

#if defined(CONFIG_RASPBERRYPI_FIRMWARE)

#include <rpi_fw.h>

#define VC_CLOCK_ID_ARM 3U
#define ARM_TARGET_HZ   1000000000U

/* Not (yet) in rpi_fw.h: under-voltage / throttle status bits.
 * Response: bit0 under-voltage now, bit1 ARM freq capped, bit2 throttled;
 * bits 16-18 = the same has-occurred-since-boot latches.
 */
#define RETRO_FW_TAG_GET_THROTTLED 0x00030046U

void retro_bump_arm_clock(void)
{
	const struct device *fw = DEVICE_DT_GET_ONE(raspberrypi_bcm283x_firmware);

	if (!device_is_ready(fw)) {
		printk("[retro] VC firmware not ready -- ARM stays at idle clock\n");
		return;
	}

	uint32_t cur[2] = {VC_CLOCK_ID_ARM, 0U};

	rpi_fw_transfer(fw, RPI_FW_TAG_GET_CLOCK_RATE, cur, sizeof(cur));

	uint32_t req[3] = {VC_CLOCK_ID_ARM, ARM_TARGET_HZ, 0U /* skip_setting_turbo */};
	int err = rpi_fw_transfer(fw, RPI_FW_TAG_SET_CLOCK_RATE, req, sizeof(req));

	uint32_t post[2] = {VC_CLOCK_ID_ARM, 0U};

	rpi_fw_transfer(fw, RPI_FW_TAG_GET_CLOCK_RATE, post, sizeof(post));

	/* The set-point can lie: MEASURED is the PLL truth, and THROTTLED
	 * says why they might disagree (under-voltage refuses turbo).
	 */
	uint32_t meas[2] = {VC_CLOCK_ID_ARM, 0U};
	uint32_t thr[1] = {0U};

	rpi_fw_transfer(fw, RPI_FW_TAG_GET_CLOCK_MEASURED, meas, sizeof(meas));
	rpi_fw_transfer(fw, RETRO_FW_TAG_GET_THROTTLED, thr, sizeof(thr));

	printk("[retro] ARM clock: %u -> %u Hz (SET err=%d), measured %u Hz, throttled 0x%x\n",
	       (unsigned int)cur[1], (unsigned int)post[1], err,
	       (unsigned int)meas[1], (unsigned int)thr[0]);
}

#else /* no VC firmware (qemu) */

void retro_bump_arm_clock(void)
{
}

#endif
