/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PiZZa Faux86 -- Zephyr entry point. Mirrors win32/main.cpp's
 * Config -> VM -> simulate() loop, with the ROMs + boot floppy handed in as
 * EmbeddedDisks over .incbin rodata (faux86_pack.S) instead of the command
 * line / files.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "Config.h"
#include "VM.h"
#include "DriveManager.h"
#include "faux86_host.h"

using namespace Faux86;

extern "C" {
extern const uint8_t faux86_bios_start[];
extern const uint8_t faux86_bios_end[];
extern const uint8_t faux86_videorom_start[];
extern const uint8_t faux86_videorom_end[];
extern const uint8_t faux86_ascii_start[];
extern const uint8_t faux86_ascii_end[];
extern const uint8_t faux86_floppy_start[];
extern const uint8_t faux86_floppy_end[];
}

extern "C" void faux86_bump_arm_clock(void);

static Faux86HostSystemInterface hostInterface;
static Faux86::VM *vm86;

int main(void)
{
	printk("[faux86] PiZZa Faux86 port -- board %s\n", CONFIG_BOARD);

	faux86_bump_arm_clock();

	Config cfg(&hostInterface);
	cfg.singleThreaded = true;
	cfg.enableAudio = false;
	cfg.enableConsole = false;
	cfg.enableMenu = false;
	cfg.cpuType = CPU_TYPE_V20;
	cfg.cpuSpeed = 0; /* uncapped: reach the POST banner faster under slow TCG */

	cfg.biosFile = new EmbeddedDisk((uint8_t *)faux86_bios_start,
		(uint64_t)(faux86_bios_end - faux86_bios_start));
	cfg.videoRomFile = new EmbeddedDisk((uint8_t *)faux86_videorom_start,
		(uint64_t)(faux86_videorom_end - faux86_videorom_start));
	cfg.asciiFile = new EmbeddedDisk((uint8_t *)faux86_ascii_start,
		(uint64_t)(faux86_ascii_end - faux86_ascii_start));
	cfg.diskDriveA = new EmbeddedDisk((uint8_t *)faux86_floppy_start,
		(uint64_t)(faux86_floppy_end - faux86_floppy_start));
	cfg.bootDrive = 0; /* fd0 = A: */

	printk("[faux86] roms: bios=%u videorom=%u ascii=%u floppy=%u bytes\n",
		(unsigned)(faux86_bios_end - faux86_bios_start),
		(unsigned)(faux86_videorom_end - faux86_videorom_start),
		(unsigned)(faux86_ascii_end - faux86_ascii_start),
		(unsigned)(faux86_floppy_end - faux86_floppy_start));

	vm86 = new VM(cfg);

	if (vm86->init()) {
		hostInterface.init(vm86);
		printk("[faux86] entering emulation loop\n");

		while (vm86->simulate()) {
			hostInterface.tick();
		}
	} else {
		printk("[faux86] VM init failed\n");
	}

	printk("[faux86] emulation ended\n");
	return 0;
}
