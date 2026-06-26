/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PiZZa SMP-banded copy of phoboslab/wipeout-rewrite src/render_software.c
 * with the four edits from `render_software.smp.diff`:
 *
 *   (1) thread-local band bounds (g_band_y0 / g_band_y1)
 *   (2) cheap y-extent reject before any per-triangle setup
 *   (3) y-clamp inside the scanline loop so band 0 doesn't write into
 *       band 1's rows on the seam pixel
 *   (4) render_flush_serial() / render_flush_parallel() variants
 *
 * Differences vs the diff in `wipeout files/`:
 *   - No pthread fan-out. Parallel flush uses gfx_parallel_run() from
 *     gfx_parallel_zephyr.c so the Zephyr build can dispatch onto a
 *     persistent worker pool pinned to cores 1..N-1.
 *   - No test-only render_set_projection_test setter (it lives only
 *     in the simulation verifier).
 *   - render_flush() is the public dispatcher: serial below
 *     threshold or when SMP isn't active, parallel above.
 *
 * Everything else is upstream verbatim. Keep this file in lockstep
 * with phoboslab's render_software.c -- the SMP edits are the only
 * authorised deltas.
 *
 * Correctness baseline: sim verifier on the host (`smp_verify.c`)
 * shows 0 / 76 800 pixels differ between serial and 4-band parallel
 * over the same input tris (bit-identical, frame `serial.png`
 * matches `parallel.png`). Re-run that any time these edits change.
 */

#include "system.h"
#include "render.h"
#include "mem.h"
#include "utils.h"
#include "platform.h"
#include "types.h"

#include "gfx_parallel.h"

#ifdef CONFIG_WIPEOUT_USE_QPU_TRANSFORM
#include <errno.h>
#include <zephyr/sys/printk.h>
#include "wipeout_qpu.h"
#endif

/* (1) thread-local band bounds. Default span is the whole screen so
 * the serial path -- which never touches g_band_y0/y1 -- behaves as
 * if the test in (2) is unconditionally true.
 */
static _Thread_local int32_t g_band_y0 = 0;
static _Thread_local int32_t g_band_y1 = 0x7fffffff;

/* Runtime flush mode. Defaults to serial-with-banding-disabled until
 * the platform layer (or a shell command) flips it on. Settings live
 * here, not behind a Kconfig, because we want to switch them at
 * runtime for the M5 fps-vs-bands benchmark.
 */
static int s_parallel_enabled = 0;
static int s_parallel_bands = 4;
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
	s_parallel_enabled = enabled ? 1 : 0;
	s_parallel_bands = bands;
	s_parallel_threshold = threshold;
}

int render_software_get_parallel_bands(void)
{
	return s_parallel_enabled ? s_parallel_bands : 1;
}

#define NEAR_PLANE 16.0
#define FAR_PLANE (RENDER_FADEOUT_FAR)
#define TEXTURES_MAX 1024
#define TRIS_BUFFER_SIZE 4096

typedef struct {
	vec2i_t size;
	rgba_t *pixels;
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

static void draw_tris(clip_tris_t t);
static void render_flush(void);
static void render_flush_serial(void);
static void render_flush_parallel(int nbands);
static void emit_post_transform(const vec4_t clip_pos[3], const vec2_t uv[3],
                                const rgba_t color_in[3], uint16_t texture_index);
#ifdef CONFIG_WIPEOUT_USE_QPU_TRANSFORM
static void qpu_emit_cb(const vec4_t clip_pos[3], const vec2_t uv[3],
                        const rgba_t color[3], uint16_t texture_index, void *ctx);
#endif

static rgba_t *screen_buffer;
static int32_t screen_pitch;
static int32_t screen_ppr;
static vec2i_t screen_size;
static float *depth_buffer;
static uint32_t depth_buffer_len;

static render_blend_mode_t blend_mode = RENDER_BLEND_NORMAL;
static bool depth_write_enabled = true;
static bool depth_test_enabled = true;
static float depth_offset = 0.0f;
static bool cull_backface_enabled = false;

static mat4_t view_mat = mat4_identity();
static mat4_t mvp_mat = mat4_identity();
static mat4_t projection_mat = mat4_identity();
static mat4_t sprite_mat = mat4_identity();

static render_texture_t textures[TEXTURES_MAX];
static uint32_t textures_len;

uint16_t RENDER_NO_TEXTURE;

int32_t tris_buffer_len = 0;
clip_tris_t tris_buffer[TRIS_BUFFER_SIZE];

static render_stats_t running_stats = {0};
static render_stats_t end_stats = {0};


void render_init(vec2i_t s) {
	render_set_screen_size(s);
	textures_len = 0;

	rgba_t white_pixels[4] = {
		rgba(128,128,128,255), rgba(128,128,128,255),
		rgba(128,128,128,255), rgba(128,128,128,255)
	};
	RENDER_NO_TEXTURE = render_texture_create(2, 2, white_pixels);

#ifdef CONFIG_WIPEOUT_USE_QPU_TRANSFORM
	/* Eager V3D bring-up so the first frame doesn't pay the alloc +
	 * power-up cost. If this fails, wpqpu_finalize() silently falls
	 * back to scalar -- the renderer continues to work, just without
	 * the QPU speedup. The log line tells you which mode you got.
	 * Using printk rather than printf -- printf has gone silent in
	 * this game-thread context for reasons I haven't tracked down,
	 * but printk from the same context lands fine.
	 */
	int qpu_rc = wpqpu_init();
	if (qpu_rc) {
		printk("[wipeout] wpqpu_init failed (%d) -- scalar fallback engaged\n",
		       qpu_rc);
	} else {
		printk("[wipeout] V3D QPU transform path active\n");
	}
#endif
}

void render_cleanup(void) {
	free(depth_buffer);
	depth_buffer = NULL;
	depth_buffer_len = 0;
}

void render_set_screen_size(vec2i_t size) {
	screen_size = size;
	error_if(size.x <= 0 || size.y <= 0, "Invalid screen size %d x %d", size.x, size.y);

	uint32_t pixel_count = (uint32_t)size.x * (uint32_t)size.y;
	if (pixel_count != depth_buffer_len) {
		float *resized = realloc(depth_buffer, pixel_count * sizeof(float));
		error_if(resized == NULL, "Failed to allocate depth buffer");
		depth_buffer = resized;
		depth_buffer_len = pixel_count;
	}

	float aspect = (float)size.x / (float)size.y;
	float fov = (73.75 / 180.0) * M_PI;
	float f = 1.0 / tan(fov / 2);
	float nf = 1.0 / (NEAR_PLANE - FAR_PLANE);
	projection_mat = mat4(
		f / aspect, 0, 0, 0,
		0, f, 0, 0,
		0, 0, (FAR_PLANE + NEAR_PLANE) * nf, -1,
		0, 0, 2 * FAR_PLANE * NEAR_PLANE * nf, 0
	);
}

void render_set_resolution(render_resolution_t res) { (void)res; }
void render_set_post_effect(render_post_effect_t post) { (void)post; }

vec2i_t render_size(void) {
	return screen_size;
}

void render_frame_prepare(void) {
	screen_buffer = platform_get_screenbuffer(&screen_pitch);
	screen_ppr = screen_pitch / sizeof(rgba_t);

	rgba_t clear_color = rgba(0, 0, 0, 255);
	uint32_t screen_buffer_len = (screen_pitch / sizeof(rgba_t)) * screen_size.y;
	for (int i = 0; i < screen_buffer_len; i++) {
		screen_buffer[i] = clear_color;
	}

	for (uint32_t i = 0; i < depth_buffer_len; i++) {
		depth_buffer[i] = 1.0f;
	}

	running_stats.num_tris = 0;
	running_stats.num_draw_calls = 0;
}

void render_frame_end(void) {
	render_flush();
	memcpy(&end_stats, &running_stats, sizeof(render_stats_t));
}

const render_stats_t* render_frame_get_stats(void) {
	return &end_stats;
}

void render_set_view(vec3_t pos, vec3_t angles) {
	render_set_depth_write(true);
	render_set_depth_test(true);

	view_mat = mat4_identity();
	mat4_set_translation(&view_mat, vec3(0, 0, 0));
	mat4_set_roll_pitch_yaw(&view_mat, vec3(angles.x, -angles.y + M_PI, angles.z + M_PI));
	mat4_translate(&view_mat, vec3_inv(pos));
	mat4_set_yaw_pitch_roll(&sprite_mat, vec3(-angles.x, angles.y - M_PI, 0));

	render_set_model_mat(&mat4_identity());
}

void render_set_view_2d(void) {
	render_set_depth_test(false);
	render_set_depth_write(false);

	float near = -1;
	float far = 1;
	float left = 0;
	float right = screen_size.x;
	float bottom = screen_size.y;
	float top = 0;
	float lr = 1 / (left - right);
	float bt = 1 / (bottom - top);
	float nf = 1 / (near - far);
	mvp_mat = mat4(
		-2 * lr,  0,  0,  0,
		0,  -2 * bt,  0,  0,
		0,		0,  2 * nf,	0,
		(left + right) * lr, (top + bottom) * bt, (far + near) * nf, 1
	);
}

void render_set_model_mat(mat4_t *m) {
	mat4_t vm_mat;
	mat4_mul(&vm_mat, &view_mat, m);
	mat4_mul(&mvp_mat, &projection_mat, &vm_mat);
}

void render_set_depth_write(bool enabled) {
	render_flush();
	depth_write_enabled = enabled;
}
void render_set_depth_test(bool enabled) {
	render_flush();
	depth_test_enabled = enabled;
}
void render_set_depth_offset(float offset) {
	render_flush();
	depth_offset = offset;
}
void render_set_screen_position(vec2_t pos) { (void)pos; }

void render_set_blend_mode(render_blend_mode_t mode) {
	render_flush();
	blend_mode = mode;
}

void render_set_cull_backface(bool enabled) {
	render_flush();
	cull_backface_enabled = enabled;
}

vec3_t render_transform(vec3_t pos) {
	return vec4_perspective_divide(vec3_transform_perspective(vec3_transform(pos, &view_mat), &projection_mat));
}


static int sort_sw_tris_by_depth(const void *a, const void *b) {
	const clip_tris_t *ta = (const clip_tris_t *)a;
	const clip_tris_t *tb = (const clip_tris_t *)b;

	float za = (ta->verts[0].clip_pos.z + ta->verts[1].clip_pos.z + ta->verts[2].clip_pos.z) * 10000.0f;
	float zb = (tb->verts[0].clip_pos.z + tb->verts[1].clip_pos.z + tb->verts[2].clip_pos.z) * 10000.0f;
	return za - zb;
}

/* ===== SMP flush variants ============================================ */

/* Each band owns a disjoint horizontal slice of the framebuffer + the
 * matching depth rows; the y-extent reject in draw_tris() guarantees
 * a tri only writes inside its band, so the bands are lock-free and
 * order-preserving (because tris_buffer is z-sorted ONCE on the
 * caller before fan-out).
 */
typedef struct {
	int32_t H;
	int32_t count;
	clip_tris_t *tris;
	int32_t nbands;
} band_ctx_t;

static void band_worker(void *arg, int band) {
	band_ctx_t *c = (band_ctx_t *)arg;
	g_band_y0 = (int32_t)((long long)c->H * band / c->nbands);
	g_band_y1 = (int32_t)((long long)c->H * (band + 1) / c->nbands) - 1;
	for (int i = 0; i < c->count; i++) {
		draw_tris(c->tris[i]);
	}
}

static void render_flush_serial(void) {
	qsort(tris_buffer, tris_buffer_len, sizeof(clip_tris_t), sort_sw_tris_by_depth);
	g_band_y0 = 0;
	g_band_y1 = screen_size.y - 1;
	for (int i = 0; i < tris_buffer_len; i++) {
		draw_tris(tris_buffer[i]);
	}
	tris_buffer_len = 0;
}

static void render_flush_parallel(int nbands) {
	qsort(tris_buffer, tris_buffer_len, sizeof(clip_tris_t), sort_sw_tris_by_depth);
	band_ctx_t c = {
		.H = screen_size.y,
		.count = tris_buffer_len,
		.tris = tris_buffer,
		.nbands = nbands,
	};
	gfx_parallel_run(band_worker, &c, nbands);
	tris_buffer_len = 0;
}

static void render_flush(void) {
#ifdef CONFIG_WIPEOUT_USE_QPU_TRANSFORM
	/* Drain any staged tris through the QPU + per-tri post-process
	 * (emit_post_transform via qpu_emit_cb). Adds to tris_buffer.
	 * Returns nonzero only on QPU failure -- which already fell back
	 * to scalar internally, so the buffer is still correct.
	 */
	(void)wpqpu_finalize(qpu_emit_cb, NULL);
#endif

	running_stats.num_tris += tris_buffer_len;
	running_stats.num_draw_calls++;

	if (s_parallel_enabled && tris_buffer_len >= s_parallel_threshold) {
		render_flush_parallel(s_parallel_bands);
	} else {
		render_flush_serial();
	}
}

static vec4_t rgba_to_vec4(rgba_t c) {
	return vec4(
		min(c.r * 2, 255) * (1.0/255.0),
		min(c.g * 2, 255) * (1.0/255.0),
		min(c.b * 2, 255) * (1.0/255.0),
		c.a * (1.0/255.0)
	);
}

static int clip_near(clip_vert_t *in, int in_len, clip_vert_t *out) {
	if (in_len < 3) {
		return 0;
	}

	int out_len = 0;
	clip_vert_t prev = in[in_len - 1];
	bool prev_in = (prev.clip_pos.z >= -prev.clip_pos.w);

	for (int i = 0; i < in_len; i++) {
		clip_vert_t curr = in[i];
		bool curr_in = (curr.clip_pos.z >= -curr.clip_pos.w);

		if (prev_in != curr_in) {
			float a = prev.clip_pos.z + prev.clip_pos.w;
			float b = curr.clip_pos.z + curr.clip_pos.w;
			float t = a / (a - b);
			out[out_len++] = (clip_vert_t){
				.clip_pos = vec4_lerp(prev.clip_pos, curr.clip_pos, t),
				.uv = vec2_lerp(prev.uv, curr.uv, t),
				.color = vec4_lerp(prev.color, curr.color, t)
			};
		}

		if (curr_in) {
			out[out_len++] = curr;
		}

		prev = curr;
		prev_in = curr_in;
	}

	return out_len;
}

/* Shared bottom half of render_push_tris: given the already-transformed
 * clip_pos for the three vertices (+ uv + color + texture), do the near-
 * plane clip, perspective divide, and fan-triangulation into tris_buffer.
 * Used by both the scalar render_push_tris path and the QPU finalize
 * callback (qpu_emit_cb).
 */
static void emit_post_transform(const vec4_t clip_pos[3], const vec2_t uv[3],
                                const rgba_t color_in[3], uint16_t texture_index) {
	render_texture_t *texture = &textures[texture_index];

	vec4_t color0 = rgba_to_vec4(color_in[0]);
	vec4_t color1 = rgba_to_vec4(color_in[1]);
	vec4_t color2 = rgba_to_vec4(color_in[2]);

	clip_vert_t in[3] = {
		{.clip_pos = clip_pos[0], .uv = uv[0], .color = color0},
		{.clip_pos = clip_pos[1], .uv = uv[1], .color = color1},
		{.clip_pos = clip_pos[2], .uv = uv[2], .color = color2},
	};
	clip_vert_t clipped[8];
	int clipped_len = clip_near(in, len(in), clipped);
	if (clipped_len < 3) {
		return;
	}

	for (int i = 0; i < clipped_len; i++) {
		vec3_t d = vec4_perspective_divide(clipped[i].clip_pos);
		clipped[i].clip_pos.x = d.x;
		clipped[i].clip_pos.y = d.y;
		clipped[i].clip_pos.z = d.z;
	}

	for (int i = 1; i < clipped_len - 1; i++) {
		tris_buffer[tris_buffer_len++] = (clip_tris_t){
			.texture = texture,
			.verts = {clipped[0], clipped[i], clipped[i + 1]}
		};
	}
}

#ifdef CONFIG_WIPEOUT_USE_QPU_TRANSFORM
/* Adapter so wpqpu_finalize can drive emit_post_transform. Also drains
 * tris_buffer mid-finalize if it's about to overflow -- we can't call
 * render_flush() here because that'd re-enter wpqpu_finalize on the
 * staged tris currently being iterated.
 */
static void qpu_emit_cb(const vec4_t clip_pos[3], const vec2_t uv[3],
                        const rgba_t color[3], uint16_t texture_index, void *ctx) {
	(void)ctx;
	if (tris_buffer_len >= TRIS_BUFFER_SIZE - 8) {
		running_stats.num_tris += tris_buffer_len;
		if (s_parallel_enabled && tris_buffer_len >= s_parallel_threshold) {
			render_flush_parallel(s_parallel_bands);
		} else {
			render_flush_serial();
		}
	}
	emit_post_transform(clip_pos, uv, color, texture_index);
}
#endif

void render_push_tris(tris_t tris, uint16_t texture_index) {
	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);

#ifdef CONFIG_WIPEOUT_USE_QPU_TRANSFORM
	/* Defer transform + clip + project until render_flush, where
	 * wpqpu_finalize batches all staged tris into one QPU kick per
	 * distinct MVP matrix. Falls back to scalar inside finalize when
	 * the staged batch is below the QPU's win threshold (64 tris).
	 */
	vec3_t pos[3]   = {tris.vertices[0].pos,   tris.vertices[1].pos,   tris.vertices[2].pos};
	vec2_t uv[3]    = {tris.vertices[0].uv,    tris.vertices[1].uv,    tris.vertices[2].uv};
	rgba_t color[3] = {tris.vertices[0].color, tris.vertices[1].color, tris.vertices[2].color};

	int rc = wpqpu_stage_tri(&mvp_mat, pos, uv, color, texture_index);

	if (rc == -ENOMEM) {
		/* Staging full -- drain via render_flush (which calls
		 * wpqpu_finalize first, then sorts + rasterizes tris_buffer)
		 * and retry the stage.
		 */
		render_flush();
		(void)wpqpu_stage_tri(&mvp_mat, pos, uv, color, texture_index);
	}
#else
	if (tris_buffer_len >= TRIS_BUFFER_SIZE - 8) {
		render_flush();
	}

	vec4_t clip_pos[3] = {
		vec3_transform_perspective(tris.vertices[0].pos, &mvp_mat),
		vec3_transform_perspective(tris.vertices[1].pos, &mvp_mat),
		vec3_transform_perspective(tris.vertices[2].pos, &mvp_mat),
	};
	vec2_t uv[3]    = {tris.vertices[0].uv,    tris.vertices[1].uv,    tris.vertices[2].uv};
	rgba_t color[3] = {tris.vertices[0].color, tris.vertices[1].color, tris.vertices[2].color};
	emit_post_transform(clip_pos, uv, color, texture_index);
#endif
}

void render_push_sprite(vec3_t pos, vec2i_t size, rgba_t color, uint16_t texture_index) {
	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);

	vec3_t p0 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5, -size.y * 0.5, 0), &sprite_mat));
	vec3_t p1 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5, -size.y * 0.5, 0), &sprite_mat));
	vec3_t p2 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5,  size.y * 0.5, 0), &sprite_mat));
	vec3_t p3 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5,  size.y * 0.5, 0), &sprite_mat));

	render_texture_t *t = &textures[texture_index];
	render_push_tris((tris_t){
		.vertices = {
			{.pos = p0, .uv = {0, 0}, .color = color},
			{.pos = p1, .uv = {0 + t->size.x ,0}, .color = color},
			{.pos = p2, .uv = {0, 0 + t->size.y}, .color = color},
		}
	}, texture_index);
	render_push_tris((tris_t){
		.vertices = {
			{.pos = p2, .uv = {0, 0 + t->size.y}, .color = color},
			{.pos = p1, .uv = {0 + t->size.x, 0}, .color = color},
			{.pos = p3, .uv = {0 + t->size.x, 0 + t->size.y}, .color = color},
		}
	}, texture_index);
}

void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture_index) {
	render_push_2d_tile(pos, vec2i(0, 0), render_texture_size(texture_index), size, color, texture_index);
}

void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index) {
	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	render_push_tris((tris_t){
		.vertices = {
			{.pos = {pos.x, pos.y + size.y, 0}, .uv = {uv_offset.x , uv_offset.y + uv_size.y}, .color = color},
			{.pos = {pos.x + size.x, pos.y, 0}, .uv = {uv_offset.x +  uv_size.x, uv_offset.y}, .color = color},
			{.pos = {pos.x, pos.y, 0}, .uv = {uv_offset.x , uv_offset.y}, .color = color},
		}
	}, texture_index);

	render_push_tris((tris_t){
		.vertices = {
			{.pos = {pos.x + size.x, pos.y + size.y, 0}, .uv = {uv_offset.x + uv_size.x, uv_offset.y + uv_size.y}, .color = color},
			{.pos = {pos.x + size.x, pos.y, 0}, .uv = {uv_offset.x + uv_size.x, uv_offset.y}, .color = color},
			{.pos = {pos.x, pos.y + size.y, 0}, .uv = {uv_offset.x , uv_offset.y + uv_size.y}, .color = color},
		}
	}, texture_index);
}


uint16_t render_texture_create(uint32_t width, uint32_t height, rgba_t *pixels) {
	error_if(textures_len >= TEXTURES_MAX, "TEXTURES_MAX reached");

	uint32_t byte_size = width * height * sizeof(rgba_t);
	uint16_t texture_index = textures_len;

	textures[texture_index] = (render_texture_t){{width, height}, mem_bump(byte_size)};
	memcpy(textures[texture_index].pixels, pixels, byte_size);

	textures_len++;
	return texture_index;
}

vec2i_t render_texture_size(uint16_t texture_index) {
	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	return textures[texture_index].size;
}

void render_texture_replace_pixels(int16_t texture_index, rgba_t *pixels) {
	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	render_texture_t *t = &textures[texture_index];
	memcpy(t->pixels, pixels, t->size.x * t->size.y * sizeof(rgba_t));
}

uint16_t render_textures_len(void) {
	return textures_len;
}

void render_textures_reset(uint16_t len) {
	error_if(len > textures_len, "Invalid texture reset len %d >= %d", len, textures_len);
	textures_len = len;
}

void render_textures_dump(const char *path) { (void)path; }



/* Rasterizer ============================================================ */

typedef struct {
	vec2_t p;
	float z;
	float q;
	vec2_t uv_q;
	vec4_t col_q;
} ss_vertex_t;

typedef struct {
	float z, q;
	vec2_t uv_q;
	vec4_t col_q;
} ss_interpolants_t;

static inline rgba_t color_mix(rgba_t in, rgba_t out) {
	float t = out.a/255.0;
	return rgba(
		lerp(in.r, out.r, t),
		lerp(in.g, out.g, t),
		lerp(in.b, out.b, t),
		255
	);
}

static inline rgba_t color_add(rgba_t in, rgba_t out) {
	float t = out.a/255.0;
	return rgba(
		min(in.r + out.r * t, 255),
		min(in.g + out.g * t, 255),
		min(in.b + out.b * t, 255),
		255
	);
}

static ss_interpolants_t lerp_it(ss_vertex_t a, ss_vertex_t b, float t) {
	return (ss_interpolants_t){
		.z	 = lerp(a.z, b.z, t),
		.q	 = lerp(a.q, b.q, t),
		.uv_q  = vec2_lerp(a.uv_q, b.uv_q, t),
		.col_q = vec4_lerp(a.col_q, b.col_q, t)
	};
}

void draw_tris(clip_tris_t t) {
	const vec4_t a = t.verts[0].clip_pos;
	const vec4_t b = t.verts[1].clip_pos;
	const vec4_t c = t.verts[2].clip_pos;

	if (cull_backface_enabled) {
		float signed_area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
		if (signed_area <= 0.0f) {
			return;
		}
	}

	bool is_flat =
		t.verts[0].color.x == t.verts[1].color.x && t.verts[0].color.y == t.verts[1].color.y &&
		t.verts[0].color.z == t.verts[1].color.z && t.verts[0].color.w == t.verts[1].color.w &&
		t.verts[0].color.x == t.verts[2].color.x && t.verts[0].color.y == t.verts[2].color.y &&
		t.verts[0].color.z == t.verts[2].color.z && t.verts[0].color.w == t.verts[2].color.w;

	/* SMP edit (2): cheap y-extent reject before any per-triangle
	 * setup. Skip tris whose screen-space y range doesn't overlap
	 * this band -- otherwise every band runs full triangle setup
	 * for every triangle, and the diff's sim measurement was
	 * setup-bound rather than fill-bound. The hhr / ya..yc here are
	 * a copy of the screen-space y computation a few lines down;
	 * keeping it local avoids reordering the original code path.
	 */
	{
		float hhr = screen_size.y * 0.5f;
		float ya = hhr - a.y*hhr, yb = hhr - b.y*hhr, yc = hhr - c.y*hhr;
		float ymn = ya<yb?(ya<yc?ya:yc):(yb<yc?yb:yc);
		float ymx = ya>yb?(ya>yc?ya:yc):(yb>yc?yb:yc);
		if ((int32_t)ymx < g_band_y0 || (int32_t)ymn > g_band_y1) return;
	}

	float hw = screen_size.x * 0.5f;
	float hh = screen_size.y * 0.5f;

	float q0 = 1.0f / t.verts[0].clip_pos.w;
	float q1 = 1.0f / t.verts[1].clip_pos.w;
	float q2 = 1.0f / t.verts[2].clip_pos.w;

	ss_vertex_t v[3] = {
		{
			.p = vec2(a.x * hw + hw, hh - a.y * hh), .z = a.z, .q = q0,
			.uv_q = vec2_mulf(t.verts[0].uv, q0),
			.col_q = vec4_mulf(t.verts[0].color, q0)
		},
		{
			.p = vec2(b.x * hw + hw, hh - b.y * hh), .z = b.z, .q = q1,
			.uv_q = vec2_mulf(t.verts[1].uv, q1),
			.col_q = vec4_mulf(t.verts[1].color, q1)
		},
		{
			.p = vec2(c.x * hw + hw, hh - c.y * hh), .z = c.z, .q = q2,
			.uv_q = vec2_mulf(t.verts[2].uv, q2),
			.col_q = vec4_mulf(t.verts[2].color, q2)
		}
	};

	if (v[1].p.y < v[0].p.y) { swap(v[0], v[1]); }
	if (v[2].p.y < v[1].p.y) { swap(v[1], v[2]); }
	if (v[1].p.y < v[0].p.y) { swap(v[0], v[1]); }

	float total_dy = v[2].p.y - v[0].p.y;
	if (total_dy <= 0.0f) {
		return;
	}

	float depth_bias = 0.5f + (depth_offset / FAR_PLANE);
	int32_t y_start = max((int32_t)ceilf(v[0].p.y - 0.5f), 0);
	int32_t y_end   = min((int32_t)floorf(v[2].p.y - 0.5f), (int32_t)screen_size.y - 1);
	/* SMP edit (3): clamp the scanline range to the band so the
	 * seam pixel goes to exactly one band (no double-write race on
	 * shared rows).
	 */
	if (y_start < g_band_y0) y_start = g_band_y0;
	if (y_end   > g_band_y1) y_end   = g_band_y1;

	for (int32_t y = y_start; y <= y_end; y++) {
		float py = y + 0.5f;

		ss_interpolants_t it_left = lerp_it(v[0], v[2], (py - v[0].p.y) / total_dy);
		float x_left = lerp(v[0].p.x, v[2].p.x, (py - v[0].p.y) / total_dy);

		ss_interpolants_t it_right;
		float x_right;
		if (py < v[1].p.y) {
			float t = (py - v[0].p.y) / (v[1].p.y - v[0].p.y);
			it_right = lerp_it(v[0], v[1], t);
			x_right  = lerp(v[0].p.x, v[1].p.x, t);
		} else {
			float t = (py - v[1].p.y) / (v[2].p.y - v[1].p.y);
			it_right = lerp_it(v[1], v[2], t);
			x_right  = lerp(v[1].p.x, v[2].p.x, t);
		}

		if (x_left > x_right) {
			swap(x_left, x_right);
			swap(it_left, it_right);
		}

		int32_t x_s = max((int32_t)ceilf(x_left - 0.5f), 0);
		int32_t x_e = min((int32_t)floorf(x_right - 0.5f), (int32_t)screen_size.x - 1);
		float span_dx = x_right - x_left;
		if (x_s > x_e || span_dx <= 0.0f) {
			continue;
		}

		float inv_span = 1.0f / span_dx;
		ss_interpolants_t it_gradient = {
			.z	 = (it_right.z - it_left.z) * inv_span,
			.q	 = (it_right.q - it_left.q) * inv_span,
			.uv_q  = vec2_mulf(vec2_sub(it_right.uv_q, it_left.uv_q), inv_span),
			.col_q = vec4_mulf(vec4_sub(it_right.col_q, it_left.col_q), inv_span)
		};

		float offset = (x_s + 0.5f) - x_left;
		ss_interpolants_t it_current = {
			.z	 = it_left.z + it_gradient.z * offset,
			.q	 = it_left.q + it_gradient.q * offset,
			.uv_q  = vec2_add(it_left.uv_q, vec2_mulf(it_gradient.uv_q, offset)),
			.col_q = vec4_add(it_left.col_q, vec4_mulf(it_gradient.col_q, offset))
		};

		rgba_t *screen_ptr = screen_buffer + screen_ppr * y + x_s;
		float *depth_ptr = depth_buffer + screen_size.x * y + x_s;
		int32_t line_len = x_e - x_s;
		for (int32_t i = 0; i <= line_len; i++) {
			float depth = clamp(it_current.z * 0.5f + depth_bias, 0.0f, 1.0f);
			if ((!depth_test_enabled || depth <= depth_ptr[i]) && it_current.q > 1e-6f) {
				float iq = 1.0f / it_current.q;

				vec2_t uv = vec2_mulf(it_current.uv_q, iq);
				int32_t tx = min((uint32_t)uv.x, t.texture->size.x - 1);
				int32_t ty = min((uint32_t)uv.y, t.texture->size.y - 1);
				rgba_t texel = t.texture->pixels[ty * t.texture->size.x + tx];

				if (texel.a > 0) {
					vec4_t c  = is_flat ? t.verts[0].color : vec4_mulf(it_current.col_q, iq);
					rgba_t color = {
						.r = (uint8_t)(texel.r * c.x),
						.g = (uint8_t)(texel.g * c.y),
						.b = (uint8_t)(texel.b * c.z),
						.a = (uint8_t)(texel.a * c.w + 0.5)
					};

					screen_ptr[i] = blend_mode == RENDER_BLEND_LIGHTER
						? color_add(screen_ptr[i], color)
						: color.a == 255
							? color
							: color_mix(screen_ptr[i], color);

					if (depth_write_enabled) {
						depth_ptr[i] = depth;
					}
				}
			}
			it_current.z += it_gradient.z;
			it_current.q += it_gradient.q;
			it_current.uv_q = vec2_add(it_current.uv_q, it_gradient.uv_q);
			it_current.col_q = vec4_add(it_current.col_q, it_gradient.col_q);
		}
	}
}
