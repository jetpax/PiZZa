/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- embedded llext core blob
 * (CONFIG_RETRO_CORE_LLEXT_EMBED). The path comes in via
 * -DRETRO_CORE_LLEXT_PATH from CMake (RETRO_CORE_LLEXT env).
 */

	.section .rodata
	.balign 8
	.global retro_core_blob_start
retro_core_blob_start:
	.incbin RETRO_CORE_LLEXT_PATH
	.global retro_core_blob_end
retro_core_blob_end:
