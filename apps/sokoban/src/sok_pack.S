/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Embedded sokoban asset pack. sokoban_assets.bin is generated into
 * the build tree by tools/pack_assets.py; the build dir is on the
 * include path for .incbin resolution.
 */

	.section .rodata
	.balign 8
	.global sok_pack_start
sok_pack_start:
	.incbin "sokoban_assets.bin"
	.global sok_pack_end
sok_pack_end:
