/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * QPU-accelerated vec3_transform_perspective for the wipeout software
 * renderer. Gated by CONFIG_WIPEOUT_USE_QPU_TRANSFORM.
 *
 * Why this exists
 * ---------------
 * render_software_smp.c::render_push_tris does three
 * vec3_transform_perspective calls per triangle on the A53. At
 * 445 ns/vert scalar, that's ~1.3 us per tri and ~4 ms per frame
 * across a typical wipeout scene -- the top item on the renderer's
 * CPU profile. The BCM2710's V3D QPUs do the same math at 48 ns/vert
 * (~9.3x faster) when given enough vertices in a single kick. This
 * module hides the staging + SoA-layout + kick mechanics behind a
 * stage/finalize API the renderer can call without knowing about
 * V3D.
 *
 * Lifecycle
 * ---------
 *   wpqpu_init() once at boot (lazy on first stage is fine too --
 *     the impl handles either; explicit init keeps the V3D bring-up
 *     out of the frame critical path).
 *   wpqpu_stage_tri() for each tri the engine pushes; copies pos +
 *     uv + color + mvp_snapshot into a private staging buffer. O(1).
 *   wpqpu_finalize(cb) at render_flush time; transforms ALL staged
 *     tris (one QPU kick per distinct mvp), then invokes cb() per
 *     tri with the resulting clip-space positions so the renderer
 *     can run its existing clip_near + perspective_divide + push-to-
 *     tris_buffer logic. Staging is cleared on return.
 *
 * Threshold
 * ---------
 * For batches below WPQPU_MIN_TRIS_FOR_KICK (currently 64 tris =
 * 192 verts = exactly 1 QPU iter per QPU), the helper falls back to
 * scalar -- QPU setup overhead dominates that small a workload. The
 * threshold is empirical, from samples/drivers/bcm2835_v3d/vec3_xform.
 */

#ifndef WIPEOUT_QPU_H_
#define WIPEOUT_QPU_H_

#include <stdint.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring V3D up + allocate persistent kernel + coherent buffers. Idempotent.
 * Returns 0 on success or a negative errno on failure (in which case the
 * renderer should fall back to scalar across the board).
 */
int wpqpu_init(void);

/* Stage one triangle for batched transform. Snapshots `mvp` and the per-
 * vertex pos/uv/color so the caller may mutate state immediately after.
 * Returns 0 on success, -ENOMEM if staging is at capacity (caller must
 * call wpqpu_finalize() and retry).
 */
int wpqpu_stage_tri(const mat4_t *mvp,
                    const vec3_t  pos[3],
                    const vec2_t  uv[3],
                    const rgba_t  color[3],
                    uint16_t      texture_index);

/* How many tris are currently staged. */
int wpqpu_stage_count(void);

/* Per-tri callback signature for finalize. `clip_pos` is the post-MVP
 * vec4_t for the three vertices; uv/color/texture pass through unchanged
 * from the stage call. `ctx` is the user pointer passed to finalize.
 */
typedef void (*wpqpu_tri_cb_t)(const vec4_t  clip_pos[3],
                               const vec2_t  uv[3],
                               const rgba_t  color[3],
                               uint16_t      texture_index,
                               void         *ctx);

/* Transform all staged tris (one QPU kick per distinct mvp; scalar
 * fallback below WPQPU_MIN_TRIS_FOR_KICK), invoke `cb` per tri with
 * the resulting clip-space positions, then clear staging.
 *
 * Returns 0 on success. On QPU failure, falls back to scalar for the
 * affected batch (so the renderer still produces correct output) and
 * returns the underlying errno; the renderer can log + continue.
 */
int wpqpu_finalize(wpqpu_tri_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* WIPEOUT_QPU_H_ */
