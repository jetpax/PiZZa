/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PiZZa Faux86 -- ARM clock bump (rpi_zero_2w).
 *
 * The VideoCore firmware idles the Cortex-A53 at arm_freq_min (~600 MHz)
 * unless something explicitly requests the turbo clock; Zephyr runs no
 * cpufreq governor. The 8086 interpreter is CPU-bound, so a boot-time
 * SET_CLOCK_RATE with skip_setting_turbo=0 pins the A53 at its rated
 * 1.0 GHz (measured ~1.6x on the Jet port). Opt-in per-app.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_RASPBERRYPI_FIRMWARE)

#include <rpi_fw.h>

#define VC_CLOCK_ID_ARM 3U
#define ARM_TARGET_HZ   1000000000U

void faux86_bump_arm_clock(void)
{
	const struct device *fw = DEVICE_DT_GET_ONE(raspberrypi_bcm283x_firmware);

	if (!device_is_ready(fw)) {
		printk("[faux86] VC firmware not ready -- ARM stays at idle clock\n");
		return;
	}

	uint32_t cur[2] = {VC_CLOCK_ID_ARM, 0U};

	rpi_fw_transfer(fw, RPI_FW_TAG_GET_CLOCK_RATE, cur, sizeof(cur));

	uint32_t req[3] = {VC_CLOCK_ID_ARM, ARM_TARGET_HZ, 0U /* skip_setting_turbo */};
	int err = rpi_fw_transfer(fw, RPI_FW_TAG_SET_CLOCK_RATE, req, sizeof(req));

	uint32_t post[2] = {VC_CLOCK_ID_ARM, 0U};

	rpi_fw_transfer(fw, RPI_FW_TAG_GET_CLOCK_RATE, post, sizeof(post));

	printk("[faux86] ARM clock: %u -> %u Hz (SET err=%d)\n",
	       (unsigned)cur[1], (unsigned)post[1], err);
}

#else /* no VC firmware (e.g. qemu) */

void faux86_bump_arm_clock(void) {}

#endif
