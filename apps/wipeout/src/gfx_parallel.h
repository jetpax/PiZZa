/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Persistent band-parallel barrier for the PiZZa software renderer.
 * Verified bit-identical to serial in simulation (4 bands; 0 / 76 800
 * pixels differ) -- the property that breaks in naive parallel
 * rasterizer rewrites holds: y-band ownership of disjoint framebuffer
 * + depth rows is lock-free and order-preserving because tris are
 * z-sorted ONCE on the caller before fan-out.
 *
 * Backend split:
 *   - Host (sim): pthreads.
 *   - Zephyr / on-board: persistent pool of CONFIG threads pinned to
 *     cores 1..N-1 (CONFIG_SMP + CONFIG_SCHED_CPU_MASK), with the
 *     caller (game thread) executing band 0 itself. Without SMP
 *     enabled at the board level, all bands fall back onto the calling
 *     core -- the band split still produces correct output but the
 *     workers contend for CPU0 and the speedup is <= 1.
 */
#ifndef GFX_PARALLEL_H
#define GFX_PARALLEL_H

#include <stdint.h>

#define GFX_PARALLEL_MAX_BANDS 4

/* Runs fn(ctx, band_index) on `nbands` workers and blocks until all
 * finish. Caller thread runs band 0 inline.
 */
typedef void (*gfx_band_fn)(void *ctx, int band);

void gfx_parallel_init(int nbands);
void gfx_parallel_run(gfx_band_fn fn, void *ctx, int nbands);

/* True iff Zephyr was built with CONFIG_SMP and at least 2 CPUs are
 * runnable. Set inside gfx_parallel_init() so the renderer can pick
 * the serial flush when SMP is absent without burning a barrier
 * round-trip.
 */
int gfx_parallel_smp_active(void);

#endif /* GFX_PARALLEL_H */
