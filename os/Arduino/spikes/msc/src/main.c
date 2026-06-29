/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Phase 6b — standalone USB Mass Storage spike (rpi_zero_2w).
 *
 * Brings up the DWC2 USB device with a single MSC LUN backed by the
 * SDHost disk "SD". MSC owns the raw disk; the host (Mac/Linux) reads
 * the partition table and mounts the FAT. We do NOT fs_mount here.
 *
 * Success = the SD appears as a removable USB volume on the host, and
 * a file written there persists across eject + power cycle.
 */

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/storage/disk_access.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* disk_name "SD" matches the zephyr,sdmmc-disk node in the board DTS. */
USBD_DEFINE_MSC_LUN(sd, "SD", "PiZZa", "sketch.llext", "0.1");

static struct usbd_context *sample_usbd;

int main(void)
{
	int rc;
	uint32_t sector_count = 0, sector_size = 0;

	/* Sanity-probe the SD so we log its geometry before MSC takes it.
	 * MSC accesses the disk on host demand; this just confirms it's
	 * present and readable. */
	rc = disk_access_init("SD");
	if (rc != 0) {
		LOG_ERR("disk_access_init(SD) failed: %d "
			"(card inserted? SDHost ok?)", rc);
	} else {
		disk_access_ioctl("SD", DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
		disk_access_ioctl("SD", DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
		LOG_INF("SD ready: %u sectors x %u bytes (%u MiB)",
			sector_count, sector_size,
			(uint32_t)(((uint64_t)sector_count * sector_size) >> 20));
	}

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		LOG_ERR("sample_usbd_init_device failed");
		return -ENODEV;
	}

	rc = usbd_enable(sample_usbd);
	if (rc != 0) {
		LOG_ERR("usbd_enable failed: %d", rc);
		return rc;
	}

	LOG_INF("PiZZa MSC spike up -- SD exposed as USB mass storage.");
	LOG_INF("Plug USB into a host; the SD's FAT volume should mount.");
	return 0;
}
