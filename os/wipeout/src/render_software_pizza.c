/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PiZZa software renderer for wipeout-rewrite, redesigned around the
 * BCM2710 / A53 / RGB565 scanout reality.
 *
 * STATUS: scaffolding only. Every public symbol is present so the
 * engine links, but every implementation is a stub. Enabling this
 * file (CONFIG_WIPEOUT_RENDERER_PIZZA=y) without finishing the phased
 * implementation will boot, log loudly, and render nothing useful.
 *
 * Spec: notes/wipeout_pizza_renderer_spec.md (read this first).
 * Inspiration: ~/github/Jet/src/Renderer.cpp -- the architectural
 *   patterns (Q16 inner loop, RGB565 paired stores, edge-function
 *   rasteriser, dedicated fast paths) are lifted from that codebase;
 *   no code is copied. Attribution noted; no functional dependency.
 * Predecessor: render_software_smp.c (the file this replaces under
 *   the build gate). Keep that file building until parity is reached.
 *
 * Design summary (one paragraph for cold pick-up):
 *   - Framebuffer: RGB565 uint16_t (was rgba_t ARGB_8888).
 *   - Atlas: split RGBA5551 (alpha-test) + RGBA4444 (alpha-blend),
 *     selected per-texture at create-time by alpha-channel scan.
 *   - Numeric pipeline: Q16-family fixed-point in the inner loop.
 *     No per-pixel float reciprocal; perspective recomputed every
 *     16 pixels (Quake1 sub-span trick).
 *   - Rasterisation: edge-function (3 weights, parallel-testable),
 *     not scanline-from-vertices. Per-triangle setup hoists.
 *   - Five specialised inner-loop fast paths chosen at triangle
 *     setup; the inner loop has no branch on path.
 *   - Depth: Q16 buffer retained for 3D paths (the engine relies on
 *     it via render_set_view); skipped in 2D/HUD fast paths where
 *     the engine already turns it off.
 *   - SMP integration unchanged: same gfx_parallel_run() shape and
 *     same g_band_y0/g_band_y1 thread-locals.
 *
 * Public API: identical to render_software_smp.c. The engine sees no
 * difference. Internal storage and pipeline are wholly rewritten.
 */

#include "system.h"
#include "render.h"
#include "mem.h"
#include "utils.h"
#include "platform.h"
#include "types.h"

#include "gfx_parallel.h"

#include <zephyr/sys/printk.h>

/* ------------------------------------------------------------------ *
 *  Compile-time configuration
 * ------------------------------------------------------------------ */

#define TEXTURES_MAX            1024
#define TRIS_BUFFER_SIZE        4096
#define SUBSPAN_PERSPECTIVE_LEN 16    /* recompute perspective every N px */

/* Texture pool tags. Stored in render_texture_t.format so the
 * triangle setup path can branch once on format and pick the right
 * inner loop.
 */
#define TEX_FMT_RGBA5551  0u  /* main atlas: 5/5/5 RGB + 1-bit alpha-test */
#define TEX_FMT_RGBA4444  1u  /* effects atlas: 4/4/4/4 with soft alpha   */

/* ------------------------------------------------------------------ *
 *  Internal types
 * ------------------------------------------------------------------ */

typedef struct {
	uint16_t w, h;
	uint8_t  format;        /* TEX_FMT_* */
	uint8_t  flags;         /* reserved */
	uint16_t *pixels;       /* RGBA5551 or RGBA4444, contiguous */
} render_texture_t;

typedef struct {
	vec4_t clip_pos;
	vec2_t uv;
	vec4_t color;
} clip_vert_t;

typedef struct {
	render_texture_t *texture;
	clip_vert_t verts[3];
} clip_tris_t;

/* ------------------------------------------------------------------ *
 *  Thread-local band bounds (preserved from SMP renderer)
 * ------------------------------------------------------------------ */

static _Thread_local int32_t g_band_y0 = 0;
static _Thread_local int32_t g_band_y1 = 0x7fffffff;

/* ------------------------------------------------------------------ *
 *  SMP parallel-flush knob (preserved interface)
 * ------------------------------------------------------------------ */

static int s_parallel_enabled   = 0;
static int s_parallel_bands     = 4;
static int s_parallel_threshold = 64;

void render_software_set_parallel(int enabled, int bands, int threshold)
{
	if (bands < 1) {
		bands = 1;
	}
	if (bands > GFX_PARALLEL_MAX_BANDS) {
		bands = GFX_PARALLEL_MAX_BANDS;
	}
	if (threshold < 1) {
		threshold = 1;
	}
	s_parallel_enabled   = enabled ? 1 : 0;
	s_parallel_bands     = bands;
	s_parallel_threshold = threshold;
}

int render_software_get_parallel_bands(void)
{
	return s_parallel_enabled ? s_parallel_bands : 1;
}

/* ------------------------------------------------------------------ *
 *  Global state (mirrors render_software_smp.c shape)
 * ------------------------------------------------------------------ */

static uint16_t *screen_buffer;          /* RGB565 backbuffer */
static int32_t   screen_pitch;           /* bytes per row */
static int32_t   screen_ppr;             /* uint16_t units per row */
static vec2i_t   screen_size;

static uint16_t *depth_buffer;           /* Q16 depth; NULL if dropped */
static uint32_t  depth_buffer_len;

static render_blend_mode_t blend_mode        = RENDER_BLEND_NORMAL;
static bool                depth_write_on    = true;
static bool                depth_test_on     = true;
static float               depth_offset      = 0.0f;
static bool                cull_backface_on  = false;

static mat4_t view_mat       = mat4_identity();
static mat4_t mvp_mat        = mat4_identity();
static mat4_t projection_mat = mat4_identity();
static mat4_t sprite_mat     = mat4_identity();

static render_texture_t textures[TEXTURES_MAX];
static uint32_t         textures_len;

uint16_t RENDER_NO_TEXTURE;

int32_t     tris_buffer_len = 0;
clip_tris_t tris_buffer[TRIS_BUFFER_SIZE];

static render_stats_t running_stats = {0};
static render_stats_t end_stats     = {0};

/* ------------------------------------------------------------------ *
 *  Forward decls
 * ------------------------------------------------------------------ */

static void render_flush(void);
static void render_flush_serial(void);
static void render_flush_parallel(int nbands);
static void draw_tris(clip_tris_t t);

/* ------------------------------------------------------------------ *
 *  PHASE 1 — Framebuffer + present path
 *
 *    Output: RGB565 backbuffer allocated, cleared per frame, blitted
 *            to VC scanout at end-of-frame. No geometry yet.
 *    Touches: this file + platform_zephyr.c (screenbuffer accessor).
 * ------------------------------------------------------------------ */

void render_init(vec2i_t s)
{
	printk("[pizza-renderer] STUB: render_init(%d, %d) -- "
	       "Phase 1 not implemented\n", s.x, s.y);
	screen_size = s;
	/* TODO Phase 1:
	 *   - Allocate uint16_t backbuffer = s.x * s.y * 2 bytes,
	 *     64-byte aligned (DMA alignment, like Jet).
	 *   - Wire platform_get_screenbuffer() / present path to RGB565.
	 *   - Initialise the 2x2 white RENDER_NO_TEXTURE (RGBA5551).
	 *   - Allocate depth buffer iff depth-test usage scan says so.
	 */
}

void render_cleanup(void)
{
	/* TODO Phase 1: free backbuffer + depth + atlas pools. */
}

void render_set_screen_size(vec2i_t size)
{
	screen_size = size;
	/* TODO Phase 1: re-allocate backbuffer + depth if dims change. */
}

void render_set_resolution(render_resolution_t res)  { (void)res; }
void render_set_post_effect(render_post_effect_t p)  { (void)p; }

vec2i_t render_size(void) { return screen_size; }

void render_frame_prepare(void)
{
	/* TODO Phase 1: clear backbuffer to backcolor (use paired stores).
	 * Clear depth_buffer to 0xFFFF if present.
	 * Reset running_stats.
	 */
}

void render_frame_end(void)
{
	render_flush();
	memcpy(&end_stats, &running_stats, sizeof(render_stats_t));
	memset(&running_stats, 0, sizeof(render_stats_t));
}

const render_stats_t *render_frame_get_stats(void)
{
	return &end_stats;
}

/* ------------------------------------------------------------------ *
 *  View / matrix state — same shape as smp renderer
 * ------------------------------------------------------------------ */

void render_set_view(vec3_t pos, vec3_t angles)
{
	(void)pos; (void)angles;
	/* TODO: build view_mat + mvp_mat (port verbatim from smp). */
}

void render_set_view_2d(void)
{
	/* TODO: orthographic projection (port verbatim from smp). */
}

void render_set_model_mat(mat4_t *m)
{
	(void)m;
	/* TODO: compose mvp_mat (port verbatim from smp). */
}

void render_set_depth_write(bool enabled)   { depth_write_on = enabled; }
void render_set_depth_test(bool enabled)    { depth_test_on  = enabled; }
void render_set_depth_offset(float offset)  { depth_offset   = offset; }
void render_set_screen_position(vec2_t pos) { (void)pos; }
void render_set_blend_mode(render_blend_mode_t m) { blend_mode = m; }
void render_set_cull_backface(bool enabled) { cull_backface_on = enabled; }

/* ------------------------------------------------------------------ *
 *  PHASE 2 — Atlas conversion + textured triangles
 *
 *    Output: PSX-affine wipeout (visible warping on tilted polys).
 *    Touches: render_texture_create scan + 5551/4444 packers; the
 *             draw_tris setup path; the Path-1/3 inner loops.
 * ------------------------------------------------------------------ */

uint16_t render_texture_create(uint32_t width, uint32_t height,
                               rgba_t *pixels)
{
	error_if(textures_len >= TEXTURES_MAX, "Too many textures (max %d)",
	         TEXTURES_MAX);

	uint16_t idx = textures_len++;
	render_texture_t *t = &textures[idx];
	t->w = (uint16_t)width;
	t->h = (uint16_t)height;
	t->flags = 0;

	/* TODO Phase 2:
	 *   1. Scan pixels[].a: if any in (1..254) -> TEX_FMT_RGBA4444
	 *      else -> TEX_FMT_RGBA5551.
	 *   2. Allocate width*height*2 bytes, convert pixels into the
	 *      chosen 16-bit layout.
	 *   3. Optionally Z-order swizzle (Axis-4, can defer).
	 *
	 * For the stub: drop the pixels on the floor so create() returns
	 * an index but the texture is non-functional. Engine will issue
	 * RENDER_NO_TEXTURE early.
	 */
	(void)pixels;
	t->format = TEX_FMT_RGBA5551;
	t->pixels = NULL;
	return idx;
}

vec2i_t render_texture_size(uint16_t idx)
{
	error_if(idx >= textures_len, "Invalid texture %d", idx);
	return (vec2i_t){textures[idx].w, textures[idx].h};
}

void render_texture_replace_pixels(int16_t idx, rgba_t *pixels)
{
	error_if(idx >= (int16_t)textures_len, "Invalid texture %d", idx);
	(void)pixels;
	/* TODO Phase 2: re-pack into existing storage at textures[idx].
	 * Must not change format vs original create() decision.
	 */
}

uint16_t render_textures_len(void)              { return (uint16_t)textures_len; }
void     render_textures_reset(uint16_t len)    { textures_len = len; }
void     render_textures_dump(const char *path) { (void)path; }

/* ------------------------------------------------------------------ *
 *  Triangle push path — engine-facing
 *
 *    The conversion from float rgba_t / vec4_t to Q16 lives here so
 *    the rasteriser only ever deals with fixed-point.
 *
 *    Phase 2 deliverable: tris_buffer fills correctly and render_flush
 *    routes through draw_tris with sensible Q16 contents.
 * ------------------------------------------------------------------ */

void render_push_tris(tris_t tris, uint16_t texture_index)
{
	(void)tris; (void)texture_index;
	/* TODO Phase 2:
	 *   - Frustum cull + clip against near plane (port from smp).
	 *   - For each output clip_tris, compute clip-space coords.
	 *   - Convert vertex colors to a single Q16 brightness if
	 *     constant-colour, else stash for per-vertex Gouraud.
	 *   - Append to tris_buffer.
	 */
}

void render_push_sprite(vec3_t pos, vec2i_t size, rgba_t color,
                        uint16_t texture_index)
{
	(void)pos; (void)size; (void)color; (void)texture_index;
	/* TODO Phase 2: port from smp; same shape, different colour conv. */
}

void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color,
                    uint16_t texture_index)
{
	render_push_2d_tile(pos, (vec2i_t){0, 0},
	                    render_texture_size(texture_index), size,
	                    color, texture_index);
}

void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size,
                         vec2i_t size, rgba_t color, uint16_t texture_index)
{
	(void)pos; (void)uv_offset; (void)uv_size; (void)size;
	(void)color; (void)texture_index;
	/* TODO Phase 2: port from smp; 2D path emits two clip_tris. */
}

/* ------------------------------------------------------------------ *
 *  PHASE 3 — Rasteriser (the actual work)
 *
 *    draw_tris() is the per-triangle dispatcher. It runs the
 *    edge-function setup, picks a fast path, and walks the bbox.
 *
 *    Phase 1+2 wire the plumbing; Phase 3 is where speedup arrives.
 * ------------------------------------------------------------------ */

static void draw_tris(clip_tris_t t)
{
	(void)t;
	/* TODO Phase 3:
	 *
	 * SETUP (per triangle, hoisted out of the inner loop):
	 *   - Project clip-space verts to screen-space (Q12.20 x,y).
	 *   - Compute edge function coefficients (w0/w1/w2 init + dx/dy).
	 *   - Compute bbox; clamp y to [g_band_y0, g_band_y1].
	 *   - Compute per-vertex 1/w in Q1.31 for sub-span perspective.
	 *   - Compute per-pixel u_step, v_step in Q12.20 from gradients.
	 *   - Decide fast path:
	 *       texture == NO_TEXTURE && flat opaque    -> Path 0
	 *       texture && opaque flat                  -> Path 1
	 *       texture && lit (per-vert or per-pixel)  -> Path 2
	 *       texture && alpha-test (5551)            -> Path 3
	 *       4444 atlas || color.a != 255            -> Path 4
	 *
	 * ROW LOOP (per y in bbox):
	 *   - Compute w0/w1/w2 at x = bbox.x_min.
	 *   - Walk x: pixel is inside iff (w0 | w1 | w2) >= 0.
	 *   - Every SUBSPAN_PERSPECTIVE_LEN pixels: recompute (u,v) via
	 *     true perspective; affine interpolate between checkpoints.
	 *   - Path-specific inner loop (see fast-path notes).
	 *
	 * Initial implementation: Path-0 only. Other paths land in
	 * subsequent commits within Phase 3.
	 */
	printk("[pizza-renderer] STUB: draw_tris -- Phase 3 not implemented\n");
}

/* ------------------------------------------------------------------ *
 *  Flush dispatchers (preserved interface; reuses gfx_parallel_run)
 * ------------------------------------------------------------------ */

static void render_flush_serial(void)
{
	g_band_y0 = 0;
	g_band_y1 = screen_size.y - 1;
	for (int32_t i = 0; i < tris_buffer_len; i++) {
		draw_tris(tris_buffer[i]);
	}
	tris_buffer_len = 0;
}

static void band_worker(int32_t band_index, int32_t band_count,
                        int32_t y_first, int32_t y_last, void *arg)
{
	(void)band_index; (void)band_count; (void)arg;
	g_band_y0 = y_first;
	g_band_y1 = y_last;
	for (int32_t i = 0; i < tris_buffer_len; i++) {
		draw_tris(tris_buffer[i]);
	}
}

static void render_flush_parallel(int nbands)
{
	gfx_parallel_run(nbands, screen_size.y, band_worker, NULL);
	tris_buffer_len = 0;
}

static void render_flush(void)
{
	if (s_parallel_enabled && tris_buffer_len >= s_parallel_threshold) {
		render_flush_parallel(s_parallel_bands);
	} else {
		render_flush_serial();
	}
}

/* ------------------------------------------------------------------ *
 *  PHASE 3 inner-loop helpers — to be filled in
 * ------------------------------------------------------------------ */

/* Path 0: opaque flat untextured.
 *   For each row, paired uint32_t store of (color << 16) | color.
 *   memset-grade. ~5% of pixels in race scene.
 */

/* Path 1: opaque flat-shaded textured.
 *   1 fetch + 1 store per pixel, paired when both lanes pass.
 *   ~40% of pixels.
 */

/* Path 2: opaque lit textured.
 *   Q16 brightness step + 3 channel modulate per pixel.
 *   ~25% of pixels (ships).
 */

/* Path 3: alpha-test textured (1-bit, RGBA5551).
 *   Branch on bit-15 of fetched texel; store-or-skip.
 *   ~25% of pixels.
 */

/* Path 4: alpha-blend (RGBA4444 atlas or color.a != 255).
 *   4-channel blend, ~10 ops per pixel. <5% of pixels (HUD/glow).
 */
