/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Zephyr backend for the band-parallel barrier. Persistent worker
 * pool: bands 1..N-1 each have a dedicated CONFIG thread parked on a
 * "go" semaphore between flushes; band 0 runs on the caller (game
 * thread).
 *
 * The cpu_pin call below is guarded on CONFIG_SCHED_CPU_MASK. Without
 * it (the rpi_zero_2w default today) the workers compete with the
 * caller for CPU0 cooperatively -- the band split still produces a
 * correct frame but speedup vs serial is <= 1. The path is here so
 * the renderer code is single-shape across SMP and UP; flip the gate
 * in render_software_smp.c (or via shell `wipeout flush parallel`)
 * to actually exercise it when SMP lands.
 */

#include <zephyr/kernel.h>
#include "gfx_parallel.h"

#define WORKER_STACK 8192

static struct k_thread       threads[GFX_PARALLEL_MAX_BANDS];
static K_THREAD_STACK_ARRAY_DEFINE(stacks, GFX_PARALLEL_MAX_BANDS, WORKER_STACK);
static struct k_sem          go[GFX_PARALLEL_MAX_BANDS];
static struct k_sem          done;
static gfx_band_fn           cur_fn;
static void                 *cur_ctx;
static int                   n_bands;
static int                   smp_active;

static void worker(void *p0, void *p1, void *p2)
{
	int band = (int)(intptr_t)p0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);

	for (;;) {
		k_sem_take(&go[band], K_FOREVER);
		cur_fn(cur_ctx, band);
		k_sem_give(&done);
	}
}

void gfx_parallel_init(int nbands)
{
	if (nbands < 1) {
		nbands = 1;
	}
	if (nbands > GFX_PARALLEL_MAX_BANDS) {
		nbands = GFX_PARALLEL_MAX_BANDS;
	}
	n_bands = nbands;
	k_sem_init(&done, 0, nbands);

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_CPU_MASK)
	smp_active = (arch_num_cpus() > 1) ? 1 : 0;
#else
	smp_active = 0;
#endif

	/* Band 0 runs on the calling thread; spawn workers for bands
	 * 1..n-1 and try to pin them, one per core. With SMP off the
	 * pin is a no-op and the workers serialize on CPU0; with SMP
	 * on, each band runs lock-free on its own core.
	 */
	for (int b = 1; b < nbands; b++) {
		k_sem_init(&go[b], 0, 1);
		k_tid_t t = k_thread_create(&threads[b], stacks[b], WORKER_STACK,
					    worker, (void *)(intptr_t)b, NULL, NULL,
					    K_PRIO_COOP(5), 0, K_NO_WAIT);
		k_thread_name_set(t, "gfx_band");
#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_CPU_MASK)
		(void)k_thread_cpu_pin(t, b);
#endif
	}
}

void gfx_parallel_run(gfx_band_fn fn, void *ctx, int nbands)
{
	if (nbands <= 1) {
		fn(ctx, 0);
		return;
	}
	if (nbands > n_bands) {
		nbands = n_bands;
	}
	cur_fn = fn;
	cur_ctx = ctx;
	for (int b = 1; b < nbands; b++) {
		k_sem_give(&go[b]);
	}
	fn(ctx, 0);
	for (int b = 1; b < nbands; b++) {
		k_sem_take(&done, K_FOREVER);
	}
}

int gfx_parallel_smp_active(void)
{
	return smp_active;
}
