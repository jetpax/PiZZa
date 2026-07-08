/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Embedded ROMs + boot floppy for the PiZZa Faux86 port. CMakeLists copies
 * assets/{pcxtbios.bin,videorom.bin,asciivga.dat,boot.img} into the build
 * tree under these .incbin names; faux86_main wraps each in an EmbeddedDisk.
 */

	.section .rodata
	.balign 8
	.global faux86_bios_start
faux86_bios_start:
	.incbin "faux86_bios.bin"
	.global faux86_bios_end
faux86_bios_end:

	.balign 8
	.global faux86_videorom_start
faux86_videorom_start:
	.incbin "faux86_videorom.bin"
	.global faux86_videorom_end
faux86_videorom_end:

	.balign 8
	.global faux86_ascii_start
faux86_ascii_start:
	.incbin "faux86_ascii.dat"
	.global faux86_ascii_end
faux86_ascii_end:

	.balign 8
	.global faux86_floppy_start
faux86_floppy_start:
	.incbin "faux86_floppy.img"
	.global faux86_floppy_end
faux86_floppy_end:
