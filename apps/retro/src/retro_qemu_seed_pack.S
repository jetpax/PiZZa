/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * qemu ram-disk seed blobs: two core llext files embedded in rodata
 * (SEED_CORE_A / SEED_CORE_B come in via -D from CMake), written to the
 * ram-disk at boot by retro_qemu_seed.c.
 */

	.section .rodata
	.balign 8
	.global seed_core_a_start
seed_core_a_start:
	.incbin SEED_CORE_A
	.global seed_core_a_end
seed_core_a_end:

	.balign 8
	.global seed_core_b_start
seed_core_b_start:
	.incbin SEED_CORE_B
	.global seed_core_b_end
seed_core_b_end:
