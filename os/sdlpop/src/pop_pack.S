/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Embedded SDLPoP asset pack (work-order §4 preferred pipeline).
 * sdlpop_assets.bin is generated into the build tree by
 * tools/pack_assets.py; the build dir is on the include path for
 * .incbin resolution.
 */

	.section .rodata
	.balign 8
	.global pop_pack_start
pop_pack_start:
	.incbin "sdlpop_assets.bin"
	.global pop_pack_end
pop_pack_end:
