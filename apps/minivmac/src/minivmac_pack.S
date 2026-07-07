/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Embedded Mac Plus ROM + boot disk. minivmac_rom.bin and minivmac_disk.dsk
 * are copied into the build tree by CMakeLists; the build dir is on the
 * include path for .incbin resolution. Served by minivmac_assets.c: the ROM
 * read-only as "vMac.ROM", the disk through a writable RAM overlay as
 * "boot.dsk".
 */

	.section .rodata
	.balign 8
	.global minivmac_rom_start
minivmac_rom_start:
	.incbin "minivmac_rom.bin"
	.global minivmac_rom_end
minivmac_rom_end:

	.balign 8
	.global minivmac_disk_start
minivmac_disk_start:
	.incbin "minivmac_disk.dsk"
	.global minivmac_disk_end
minivmac_disk_end:
