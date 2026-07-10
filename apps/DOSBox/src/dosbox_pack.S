/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Embedded FAT disk image (assets/*.img, copied into the build tree as
 * dosbox_disk.img by CMakeLists). Served writable via a RAM copy by
 * dosbox_assets.c; IMGMOUNT mounts it.
 */

	.section .rodata
	.balign 8
	.global dbx_disk_start
dbx_disk_start:
	.incbin "dosbox_disk.img"
	.global dbx_disk_end
dbx_disk_end:
