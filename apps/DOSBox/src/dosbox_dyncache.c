/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa DOSBox-X -- dynrec code-cache backing (P0 dual-map W^X).
 *
 * Zephyr's arm64 MMU force-PXNs any writable normal mapping, so
 * dosbox-x's single-RWX and malloc code-cache paths cannot execute.
 * P0 (pizza-dynrec-probe, HW-verified on rpi_zero_2w) proved the fix:
 * a static arena mapped twice -- one RW alias to emit blocks, one RX
 * alias to run them, constant delta between them -- with stock
 * __builtin___clear_cache flushing on the RW VA. dosbox-x's dual-map
 * machinery (cache_rwtox / DYNCOREM_DUAL_RW_X) consumes exactly this.
 *
 * The patched dynamic_alloc_common.h (build-dir gen copy) calls
 * pizza_dyncache_map() as its first allocation attempt.
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/sys/printk.h>

/* kernel-internal arch API (kernel/include/kernel_arch_interface.h) */
extern void arch_mem_map(void *virt, uintptr_t phys, size_t size, uint32_t flags);

/* CACHE_TOTAL(8M) + CACHE_MAXSIZE(8K) + PAGESIZE_TEMP, rounded up to a
 * whole number of 64 KiB MMU granules (129 * 64 KiB = 8.4 MiB).
 */
#define ARENA_SIZE (129u * 64u * 1024u)
#define VA_RW      0x80000000UL
#define VA_RX      0x81000000UL

static uint8_t dyn_arena[ARENA_SIZE] __aligned(64 * 1024);
static bool mapped;

/* returns the RW alias base; *exec_ptr = the RX alias base */
uint8_t *pizza_dyncache_map(size_t size, uint8_t **exec_ptr)
{
	if (size > ARENA_SIZE) {
		printk("[dbx] dyncache: request %zu > arena %u\n", size, ARENA_SIZE);
		return NULL;
	}
	if (!mapped) {
		uintptr_t pa = (uintptr_t)dyn_arena; /* flat map, VM_OFFSET=0 */

		arch_mem_map((void *)VA_RW, pa, ARENA_SIZE,
			     K_MEM_CACHE_WB | K_MEM_PERM_RW);
		arch_mem_map((void *)VA_RX, pa, ARENA_SIZE,
			     K_MEM_CACHE_WB | K_MEM_PERM_EXEC);
		mapped = true;
		printk("[dbx] dyncache: %u KiB arena, RW@0x%lx RX@0x%lx (phys 0x%lx)\n",
		       ARENA_SIZE / 1024, VA_RW, VA_RX, pa);
	}
	*exec_ptr = (uint8_t *)VA_RX;
	return (uint8_t *)VA_RW;
}
