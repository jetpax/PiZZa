/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Zephyr backend for the band-parallel barrier. Persistent worker
 * pool: bands 1..N-1 each have a dedicated thread pinned to its own
 * cpu and parked on a "go" semaphore between flushes; band 0 runs on
 * the caller (game thread on cpu 0).
 *
 * SMP on rpi_zero_2w: workers run on cpus 1..N-1 via k_thread_cpu_pin.
 * Cross-cpu wake-ups go through the BCM2836 mailbox-0 scheduler IPI
 * in the SoC driver; sustained ~30 fps at 1 GHz on race scenes,
 * ~1.3x per-tri speedup vs serial. Init creates the workers in
 * K_FOREVER state, installs the pin, then k_thread_start so the
 * worker can't race-preempt before the pin lands.
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
	unsigned int my_cpu = arch_curr_cpu()->id;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);

	/* Once-per-worker confirmation that secondaries actually dispatch
	 * pinned threads. The match-up (band N alive on cpu N) is load-
	 * bearing for any post-mortem of the cross-cpu IPI hang.
	 */
	printk("[gfx_band %d] alive on cpu %u\n", band, my_cpu);

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
	int n_online = (int)arch_num_cpus();
#else
	int n_online = 1;
#endif
	smp_active = (n_online > 1) ? 1 : 0;
	printk("[gfx_parallel] init nbands=%d  arch_num_cpus=%d  smp_active=%d\n",
	       nbands, n_online, smp_active);

	/* Band 0 runs on the calling thread; spawn workers for bands
	 * 1..n-1 SUSPENDED, install the cpu pin, then start. With
	 * K_NO_WAIT the K_PRIO_COOP(5) worker preempts the init code and
	 * runs once on cpu 0 before the pin lands -- making its first
	 * sem_take park it on cpu 0's runqueue. K_FOREVER + start avoids
	 * that.
	 *
	 * Only pin when arch_num_cpus() reports cpu b online; pinning to
	 * a parked secondary makes the worker un-schedulable on UP.
	 */
	for (int b = 1; b < nbands; b++) {
		k_sem_init(&go[b], 0, 1);
		k_tid_t t = k_thread_create(&threads[b], stacks[b], WORKER_STACK,
					    worker, (void *)(intptr_t)b, NULL, NULL,
					    K_PRIO_COOP(5), 0, K_FOREVER);
		k_thread_name_set(t, "gfx_band");
#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_CPU_MASK)
		if (b < n_online) {
			int rc = k_thread_cpu_pin(t, b);

			printk("[gfx_parallel] band %d -> cpu %d (rc=%d)\n", b, b, rc);
		} else {
			printk("[gfx_parallel] band %d unpinned (cpu %d not online)\n", b, b);
		}
#endif
		k_thread_start(t);
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
