/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * QPU-accelerated vec3_transform_perspective implementation. See
 * wipeout_qpu.h for the public API contract and architectural rationale.
 *
 * Shader: vec3_xform_code (compiled from
 * samples/drivers/bcm2835_v3d/vec3_xform/src/vec3_xform.qasm and
 * shipped verbatim in this tree as vec3_xform_code.{c,h}). The shader
 * expects SoA input -- 16 x's | 16 y's | 16 z's per iter per QPU --
 * which we lay out by gathering all staged tri positions into a flat
 * AoS buffer at stage time and scattering to the SoA layout at flush.
 *
 * Floating-point caveat: the QPU rounds toward zero on fmul/fadd,
 * so transformed clip positions differ from the scalar reference by
 * up to ~16 ULP after the 3-mul/3-add chain. At vertex magnitudes
 * (~1e2) that's ~7e-6 -- orders of magnitude below the framebuffer's
 * sub-pixel grid, invisible in the rendered output. See
 * memory/feedback_vc4_qpu_rounds_toward_zero.md for the analysis.
 */

#include <string.h>
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/misc/bcm2835_v3d.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wipeout_qpu.h"
#include "vec3_xform_code.h"

LOG_MODULE_REGISTER(wipeout_qpu, LOG_LEVEL_INF);

#define V3D_NODE DT_NODELABEL(v3d)

/* Capped at half of render_software_smp.c's TRIS_BUFFER_SIZE (4096)
 * so the post-clip output -- which can be up to 2x the input via
 * clip_near's near-plane-crossing splits -- never overflows the
 * downstream buffer. wipeout's typical per-frame triangle count
 * (~3000) splits cleanly across 2 finalize calls at this cap.
 */
#define WPQPU_STAGED_TRIS_MAX   2048
#define WPQPU_STAGED_VERTS_MAX  (WPQPU_STAGED_TRIS_MAX * 3)

/* Below this, the QPU's fixed setup cost (~14 us) outweighs the win
 * over scalar. Empirical from samples/drivers/bcm2835_v3d/vec3_xform
 * speedup curve: 64 tris = 192 verts = exactly 1 iter per QPU, the
 * first point on the curve where the QPU shows a meaningful speedup
 * over scalar.
 */
#define WPQPU_MIN_TRIS_FOR_KICK 64

/* Distinct MVP matrices a single staged batch can hold. wipeout
 * typically uses 1-3 per frame (one per drawn model). 16 is generous.
 */
#define WPQPU_MVPS_MAX          16

/* Per-QPU kernel layout matches samples/drivers/bcm2835_v3d/vec3_xform:
 *   NUM_QPUS = 12 (saturates the silicon without oversubscription)
 *   LANES    = 16 (QPU SIMD width)
 *   NUM_UNIFS = 4 + 16 (qpu_num + in_addr + out_addr + iters + mat4)
 *
 * MAX_ITERS_PER_QPU caps the per-kick QPU work. ITERS=64 supports
 * 64 * 16 * 12 = 12288 verts/kick, well above our 4096 * 3 = 12288
 * upper bound -- the rare frame that pushes more than 4096 tris
 * would flush mid-way through render_push_tris's overflow check,
 * which lands us back at <= 12288 verts per kick.
 */
#define WPQPU_NUM_QPUS          12u
#define WPQPU_LANES             16u
#define WPQPU_NUM_UNIFS         (4u + 16u)
#define WPQPU_MAX_ITERS_PER_QPU 64u
#define WPQPU_MAX_VERTS_PER_KICK \
	(WPQPU_NUM_QPUS * WPQPU_MAX_ITERS_PER_QPU * WPQPU_LANES)

/* Coherent input/output byte sizes for a max-sized kick. */
#define WPQPU_IN_BYTES  (WPQPU_NUM_QPUS * WPQPU_MAX_ITERS_PER_QPU * 3u * WPQPU_LANES * sizeof(float))
#define WPQPU_OUT_BYTES (WPQPU_NUM_QPUS * WPQPU_MAX_ITERS_PER_QPU * 4u * WPQPU_LANES * sizeof(float))

#define V3D_MEM_FLAG_L1_NONALLOC 0x4u

/* ------------------------------------------------------------------ *
 *  Staged-tri table (AoS, CPU memory).
 * ------------------------------------------------------------------ */

typedef struct {
	vec3_t   pos[3];
	vec2_t   uv[3];
	rgba_t   color[3];
	uint16_t texture_index;
	uint16_t mvp_index;
} wpqpu_staged_tri_t;

static wpqpu_staged_tri_t s_tris[WPQPU_STAGED_TRIS_MAX];
static int                s_tris_len;

static mat4_t  s_mvps[WPQPU_MVPS_MAX];
static int     s_mvps_len;
/* Cache the last (mvp, index) lookup -- consecutive same-mvp pushes
 * are the common case and this avoids the memcmp scan.
 */
static int     s_last_mvp_index = -1;

/* ------------------------------------------------------------------ *
 *  V3D resources (lazy-init).
 * ------------------------------------------------------------------ */

static const struct device   *s_v3d;
static struct bcm2835_v3d_kernel s_kernel;
static uintptr_t  s_in_bus;
static uintptr_t  s_out_bus;
static float     *s_in_cpu;
static float     *s_out_cpu;
static uint32_t   s_in_handle;
static uint32_t   s_out_handle;
static bool       s_initialised;

/* Stats for diagnosing the integration. Per-finalize counters since
 * init -- printed every WPQPU_STATS_LOG_EVERY finalize calls so the
 * shell log doesn't flood, and exposed via wpqpu_dump_stats() if a
 * caller wants an on-demand readout.
 */
#define WPQPU_STATS_LOG_EVERY 256u
static uint32_t s_n_finalize;
static uint32_t s_n_qpu_groups;
static uint32_t s_n_scalar_groups;
static uint32_t s_qpu_verts;
static uint32_t s_scalar_verts;
static uint64_t s_qpu_total_cycles;
static uint64_t s_scalar_total_cycles;
static uint64_t s_layout_total_cycles;  /* scatter + gather */

/* ------------------------------------------------------------------ *
 *  init / teardown
 * ------------------------------------------------------------------ */

int wpqpu_init(void)
{
	if (s_initialised) {
		return 0;
	}

	s_v3d = DEVICE_DT_GET(V3D_NODE);
	if (!device_is_ready(s_v3d)) {
		LOG_ERR("v3d device not ready");
		return -ENODEV;
	}

	void     *in_cpu  = NULL;
	void     *out_cpu = NULL;
	int rc = bcm2835_v3d_alloc_coherent(s_v3d, WPQPU_IN_BYTES, 64u,
	                                    V3D_MEM_FLAG_L1_NONALLOC,
	                                    &s_in_bus, &in_cpu, &s_in_handle);
	if (rc) {
		LOG_ERR("alloc_coherent(in, %u) rc=%d", (uint32_t)WPQPU_IN_BYTES, rc);
		return rc;
	}
	rc = bcm2835_v3d_alloc_coherent(s_v3d, WPQPU_OUT_BYTES, 64u,
	                                V3D_MEM_FLAG_L1_NONALLOC,
	                                &s_out_bus, &out_cpu, &s_out_handle);
	if (rc) {
		LOG_ERR("alloc_coherent(out, %u) rc=%d", (uint32_t)WPQPU_OUT_BYTES, rc);
		(void)bcm2835_v3d_free_coherent(s_v3d, s_in_handle);
		return rc;
	}
	s_in_cpu  = in_cpu;
	s_out_cpu = out_cpu;

	rc = bcm2835_v3d_kernel_init(s_v3d, &s_kernel, WPQPU_NUM_QPUS,
	                             WPQPU_NUM_UNIFS,
	                             vec3_xform_code, sizeof(vec3_xform_code));
	if (rc) {
		LOG_ERR("kernel_init rc=%d", rc);
		(void)bcm2835_v3d_free_coherent(s_v3d, s_in_handle);
		(void)bcm2835_v3d_free_coherent(s_v3d, s_out_handle);
		return rc;
	}

	s_initialised = true;
	LOG_INF("wpqpu ready: in=%uKB out=%uKB max_verts/kick=%u",
		(uint32_t)(WPQPU_IN_BYTES / 1024), (uint32_t)(WPQPU_OUT_BYTES / 1024),
		(uint32_t)WPQPU_MAX_VERTS_PER_KICK);
	return 0;
}

/* ------------------------------------------------------------------ *
 *  staging
 * ------------------------------------------------------------------ */

static int mvp_intern(const mat4_t *m)
{
	if (s_last_mvp_index >= 0 &&
	    memcmp(&s_mvps[s_last_mvp_index], m, sizeof(mat4_t)) == 0) {
		return s_last_mvp_index;
	}
	for (int i = 0; i < s_mvps_len; i++) {
		if (memcmp(&s_mvps[i], m, sizeof(mat4_t)) == 0) {
			s_last_mvp_index = i;
			return i;
		}
	}
	if (s_mvps_len >= WPQPU_MVPS_MAX) {
		return -ENOSPC;
	}
	int idx = s_mvps_len++;
	s_mvps[idx] = *m;
	s_last_mvp_index = idx;
	return idx;
}

int wpqpu_stage_tri(const mat4_t *mvp,
                    const vec3_t  pos[3],
                    const vec2_t  uv[3],
                    const rgba_t  color[3],
                    uint16_t      texture_index)
{
	if (s_tris_len >= WPQPU_STAGED_TRIS_MAX) {
		return -ENOMEM;
	}
	int mvp_idx = mvp_intern(mvp);

	if (mvp_idx < 0) {
		/* Out of MVP slots -- bubble up so caller can flush. */
		return -ENOMEM;
	}

	wpqpu_staged_tri_t *t = &s_tris[s_tris_len++];

	memcpy(t->pos, pos, sizeof(t->pos));
	memcpy(t->uv, uv, sizeof(t->uv));
	memcpy(t->color, color, sizeof(t->color));
	t->texture_index = texture_index;
	t->mvp_index     = (uint16_t)mvp_idx;
	return 0;
}

int wpqpu_stage_count(void)
{
	return s_tris_len;
}

/* ------------------------------------------------------------------ *
 *  finalize
 * ------------------------------------------------------------------ */

/* Scalar reference (mirrors types.c::vec3_transform_perspective). Used
 * for sub-threshold batches and as the QPU-failure fallback.
 */
static inline vec4_t scalar_vec3_transform_perspective(vec3_t a, const mat4_t *mat)
{
	return vec4(
		mat->m[0] * a.x + mat->m[4] * a.y + mat->m[ 8] * a.z + mat->m[12],
		mat->m[1] * a.x + mat->m[5] * a.y + mat->m[ 9] * a.z + mat->m[13],
		mat->m[2] * a.x + mat->m[6] * a.y + mat->m[10] * a.z + mat->m[14],
		mat->m[3] * a.x + mat->m[7] * a.y + mat->m[11] * a.z + mat->m[15]);
}

static void scalar_process_group(wpqpu_tri_cb_t cb, void *ctx,
                                 int mvp_idx, const int *tri_indices, int n_tris)
{
	const mat4_t *m = &s_mvps[mvp_idx];

	for (int k = 0; k < n_tris; k++) {
		wpqpu_staged_tri_t *t = &s_tris[tri_indices[k]];
		vec4_t clip_pos[3] = {
			scalar_vec3_transform_perspective(t->pos[0], m),
			scalar_vec3_transform_perspective(t->pos[1], m),
			scalar_vec3_transform_perspective(t->pos[2], m),
		};

		cb(clip_pos, t->uv, t->color, t->texture_index, ctx);
	}
}

/* QPU pass for one mvp group. tri_indices[] points into s_tris[].
 *
 * Performance notes (each cost paid once per kick):
 *
 *   - NO memset of input buffer. Padded lanes get whatever DRAM holds;
 *     the QPU computes garbage we never read. ~70 us-2 ms saved per
 *     kick at WPQPU_MAX_VERTS_PER_KICK.
 *
 *   - Scatter loop writes ALL x's for an iter, THEN all y's, THEN all
 *     z's -- consecutive writes hit the same 64-byte cache line so
 *     ARM's write-combining buffer can coalesce them into one DRAM
 *     burst per cache line instead of 3 per vert. Same for gather.
 *
 *   - We always use all WPQPU_NUM_QPUS QPUs; the SRQ scheduler handles
 *     sub-min batches fine, just with wasted lanes.
 */
static int qpu_process_group(wpqpu_tri_cb_t cb, void *ctx,
                             int mvp_idx, const int *tri_indices, int n_tris)
{
	const mat4_t *m = &s_mvps[mvp_idx];
	uint32_t n_verts = (uint32_t)(n_tris * 3);

	if (n_verts > WPQPU_MAX_VERTS_PER_KICK) {
		LOG_ERR("group too large: %u verts > %u", n_verts,
			(uint32_t)WPQPU_MAX_VERTS_PER_KICK);
		return -EINVAL;
	}

	uint32_t layout_t0 = k_cycle_get_32();

	uint32_t verts_per_qpu_min = (n_verts + WPQPU_NUM_QPUS - 1) / WPQPU_NUM_QPUS;
	uint32_t iters_per_qpu = (verts_per_qpu_min + WPQPU_LANES - 1) / WPQPU_LANES;

	if (iters_per_qpu == 0u) {
		iters_per_qpu = 1u;
	}

	uint32_t in_stride_floats  = iters_per_qpu * 3u * WPQPU_LANES;
	uint32_t out_stride_floats = iters_per_qpu * 4u * WPQPU_LANES;
	uint32_t in_stride_bytes   = in_stride_floats * sizeof(float);
	uint32_t out_stride_bytes  = out_stride_floats * sizeof(float);

	uint32_t verts_per_qpu = iters_per_qpu * WPQPU_LANES;

	/* Fused scatter: walk verts in their natural target order
	 * (QPU 0 lane 0..15 of iter 0, then iter 1, ..., then QPU 1).
	 * One inner loop per iter writes xs/ys/zs adjacent — same store
	 * count as the previous 3-sub-loop variant, but 3x less index
	 * arithmetic, and at Device-nGnRnE memory nothing coalesces
	 * either way (no Gathering by ARM-ARM definition) so we lose
	 * no bandwidth optimization by fusing.
	 */
	for (uint32_t q = 0; q < WPQPU_NUM_QPUS; q++) {
		float *qpu_base = s_in_cpu + q * in_stride_floats;
		uint32_t v_base = q * verts_per_qpu;

		for (uint32_t it = 0; it < iters_per_qpu; it++) {
			float *xs = qpu_base + it * 3u * WPQPU_LANES + 0 * WPQPU_LANES;
			float *ys = xs + WPQPU_LANES;
			float *zs = ys + WPQPU_LANES;
			uint32_t v0 = v_base + it * WPQPU_LANES;

			for (uint32_t lane = 0; lane < WPQPU_LANES; lane++) {
				uint32_t v = v0 + lane;

				if (v >= n_verts) {
					/* Padding lane: leave DRAM untouched. QPU
					 * computes garbage from whatever's there but
					 * we never read those output lanes.
					 */
					break;
				}
				wpqpu_staged_tri_t *t = &s_tris[tri_indices[v / 3]];
				vec3_t *p = &t->pos[v % 3];

				xs[lane] = p->x;
				ys[lane] = p->y;
				zs[lane] = p->z;
			}
		}
	}

	bcm2835_v3d_kernel_reset_unifs(&s_kernel);
	for (uint32_t q = 0; q < WPQPU_NUM_QPUS; q++) {
		uint32_t in_slice  = (uint32_t)s_in_bus  + q * in_stride_bytes;
		uint32_t out_slice = (uint32_t)s_out_bus + q * out_stride_bytes;

		(void)bcm2835_v3d_kernel_load_unif_u32(&s_kernel, q, q);
		(void)bcm2835_v3d_kernel_load_unif_u32(&s_kernel, q, in_slice);
		(void)bcm2835_v3d_kernel_load_unif_u32(&s_kernel, q, out_slice);
		(void)bcm2835_v3d_kernel_load_unif_u32(&s_kernel, q, iters_per_qpu);

		for (uint32_t i = 0; i < 16; i++) {
			(void)bcm2835_v3d_kernel_load_unif_f32(&s_kernel, q, m->m[i]);
		}
	}

	uint32_t scatter_done = k_cycle_get_32();
	uint32_t qpu_t0 = scatter_done;
	int rc = bcm2835_v3d_kernel_execute(s_v3d, &s_kernel);

	if (rc) {
		LOG_ERR("kernel_execute rc=%d -- falling back to scalar", rc);
		return rc;
	}
	uint32_t qpu_done = k_cycle_get_32();

	/* Gather + per-tri callback. Triangles can span iter boundaries
	 * (16 verts/iter isn't divisible by 3) and even QPU boundaries
	 * when verts_per_qpu isn't divisible by 3, so we walk verts in
	 * global order (v = 0..n_verts-1) accumulating into a 3-slot
	 * clip_pos_buf and firing the callback every 3rd vert. Reads
	 * are uncached (Device-nGnRnE) so 4 reads per vert each take a
	 * full DRAM round-trip -- the dominant cost of this stage on
	 * BCM2710. K_MEM_ARM_NORMAL_NC mapping with a pre-read DSB
	 * would let the bus burst the reads; deferred until we see
	 * whether it actually matters.
	 */
	{
		vec4_t clip_pos_buf[3];

		for (uint32_t v = 0; v < n_verts; v++) {
			uint32_t q        = v / verts_per_qpu;
			uint32_t v_in_qpu = v - q * verts_per_qpu;
			uint32_t it       = v_in_qpu / WPQPU_LANES;
			uint32_t lane     = v_in_qpu % WPQPU_LANES;
			uint32_t vert_in_tri  = v % 3u;
			uint32_t tri_in_group = v / 3u;

			float *qpu_base = s_out_cpu + q * out_stride_floats;
			float *base     = qpu_base + it * 4u * WPQPU_LANES;

			clip_pos_buf[vert_in_tri].x = base[0 * WPQPU_LANES + lane];
			clip_pos_buf[vert_in_tri].y = base[1 * WPQPU_LANES + lane];
			clip_pos_buf[vert_in_tri].z = base[2 * WPQPU_LANES + lane];
			clip_pos_buf[vert_in_tri].w = base[3 * WPQPU_LANES + lane];

			if (vert_in_tri == 2u) {
				wpqpu_staged_tri_t *t = &s_tris[tri_indices[tri_in_group]];
				cb(clip_pos_buf, t->uv, t->color, t->texture_index, ctx);
			}
		}
	}

	uint32_t gather_done = k_cycle_get_32();

	s_qpu_total_cycles    += (qpu_done    - qpu_t0);
	s_layout_total_cycles += (scatter_done - layout_t0) + (gather_done - qpu_done);
	return 0;
}

int wpqpu_finalize(wpqpu_tri_cb_t cb, void *ctx)
{
	if (s_tris_len == 0) {
		return 0;
	}

	int worst_rc = 0;

	s_n_finalize++;

	for (int m = 0; m < s_mvps_len; m++) {
		static int   indices[WPQPU_STAGED_TRIS_MAX];
		int          n = 0;

		for (int i = 0; i < s_tris_len; i++) {
			if (s_tris[i].mvp_index == m) {
				indices[n++] = i;
			}
		}
		if (n == 0) {
			continue;
		}

		int rc;

		if (n >= WPQPU_MIN_TRIS_FOR_KICK && s_initialised) {
			rc = qpu_process_group(cb, ctx, m, indices, n);
			if (rc != 0) {
				uint32_t t0 = k_cycle_get_32();
				scalar_process_group(cb, ctx, m, indices, n);
				s_scalar_total_cycles += (k_cycle_get_32() - t0);
				s_n_scalar_groups++;
				s_scalar_verts += (uint32_t)(n * 3);
				worst_rc = rc;
			} else {
				s_n_qpu_groups++;
				s_qpu_verts += (uint32_t)(n * 3);
			}
		} else {
			uint32_t t0 = k_cycle_get_32();
			scalar_process_group(cb, ctx, m, indices, n);
			s_scalar_total_cycles += (k_cycle_get_32() - t0);
			s_n_scalar_groups++;
			s_scalar_verts += (uint32_t)(n * 3);
		}
	}

	if ((s_n_finalize % WPQPU_STATS_LOG_EVERY) == 0u) {
		uint32_t cps = (uint32_t)sys_clock_hw_cycles_per_sec();
		uint32_t qpu_us    = (uint32_t)((s_qpu_total_cycles    * 1000000ULL) / cps);
		uint32_t scalar_us = (uint32_t)((s_scalar_total_cycles * 1000000ULL) / cps);
		uint32_t layout_us = (uint32_t)((s_layout_total_cycles * 1000000ULL) / cps);

		LOG_INF("finalize#%u: qpu_groups=%u (%u verts, %u us)  "
			"scalar_groups=%u (%u verts, %u us)  layout=%u us",
			s_n_finalize,
			s_n_qpu_groups, s_qpu_verts, qpu_us,
			s_n_scalar_groups, s_scalar_verts, scalar_us,
			layout_us);
	}

	s_tris_len       = 0;
	s_mvps_len       = 0;
	s_last_mvp_index = -1;
	return worst_rc;
}
