/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Embedded Doom IWAD. doom_iwad.wad is copied into the build tree by
 * CMakeLists (from DOOM_WAD); the build dir is on the include path for
 * .incbin resolution. Served by doom_assets.c through the read-only fd
 * layer.
 */

	.section .rodata
	.balign 8
	.global doom_wad_start
doom_wad_start:
	.incbin "doom_iwad.wad"
	.global doom_wad_end
doom_wad_end:
