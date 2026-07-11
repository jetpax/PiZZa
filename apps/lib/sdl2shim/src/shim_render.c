/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- software SDL_Renderer / SDL_Texture engine.
 *
 * The first shim consumers (Doom, SDLPoP, Mini vMac) each composed
 * frames app-side because their formats diverged. sdl2-sokoban is
 * written against the SDL_Renderer API proper -- and future ports
 * (SNES9x-SDL) hit the same surface -- so the engine lives here, once:
 * an ARGB8888 backing buffer the size of the single implicit window,
 * malloc'd ARGB8888 textures (any accepted format label is stored as
 * ARGB internally), nearest-neighbour scaled RenderCopy with NONE /
 * BLEND, Bresenham lines, rect fills, render-to-texture targets, and
 * RenderPresent pushing the backing through the existing s2s_present
 * seam (the app still owns the present backend TU, as always).
 *
 * Not implemented (fail loudly / extend when a consumer needs them):
 * RenderCopyEx rotation, MOD/ADD blend, non-nearest filtering (the
 * scale-quality hint is accepted and ignored), logical size != window
 * size scaling at present time.
 *
 * Dual-target: builds under Zephyr and in the host tests.
 */

#include "s2s_compat.h"
#include "sdl2shim.h"

S2S_LOG_REGISTER(s2s_render);

struct SDL_Texture {
	Uint32 format;        /* requested label; storage is always ARGB8888 */
	int access;
	int w, h;
	int pitch;            /* bytes */
	uint32_t *pixels;
	SDL_BlendMode blend;
};

struct SDL_Renderer {
	int out_w, out_h;     /* backing dims == window dims */
	int logical_w, logical_h;
	uint32_t *backing;
	uint32_t draw_color;  /* ARGB */
	SDL_Texture *target;  /* NULL = backing */
	Uint32 frame_ms;      /* PRESENTVSYNC pacing; 0 = uncapped */
	Uint32 last_present;
};

static struct SDL_Renderer the_renderer;

/* ── target helpers ──────────────────────────────────────────── */

static uint32_t *target_pixels(SDL_Renderer *r)
{
	return (r->target != NULL) ? r->target->pixels : r->backing;
}

static int target_w(SDL_Renderer *r)
{
	return (r->target != NULL) ? r->target->w : r->out_w;
}

static int target_h(SDL_Renderer *r)
{
	return (r->target != NULL) ? r->target->h : r->out_h;
}

/* ── renderer lifecycle ──────────────────────────────────────── */

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	ARG_UNUSED(index);

	if (window == NULL) {
		s2s_set_error("CreateRenderer: NULL window");
		return NULL;
	}
	if (the_renderer.backing != NULL) {
		s2s_set_error("CreateRenderer: single renderer already exists");
		return NULL;
	}

	int w, h;

	SDL_GetWindowSize(window, &w, &h);
	if (w <= 0 || h <= 0) {
		s2s_set_error("CreateRenderer: bad window size %dx%d", w, h);
		return NULL;
	}

	the_renderer.backing = calloc((size_t)w * h, 4);
	if (the_renderer.backing == NULL) {
		s2s_set_error("CreateRenderer: out of memory (%dx%d backing)", w, h);
		return NULL;
	}
	the_renderer.out_w = w;
	the_renderer.out_h = h;
	the_renderer.logical_w = w;
	the_renderer.logical_h = h;
	the_renderer.draw_color = 0xFF000000u;
	the_renderer.target = NULL;

	/* PRESENTVSYNC: pace RenderPresent to ~60 Hz. There is no real
	 * vblank, but honoring the request matters beyond fidelity: a
	 * game loop that never sleeps would starve every lower-priority
	 * thread (shell, USB, scripted input) on the single main thread.
	 */
	the_renderer.frame_ms = (flags & SDL_RENDERER_PRESENTVSYNC) ? 16 : 0;
	the_renderer.last_present = 0;

	int rc = s2s_present_init(w, h);

	if (rc != 0) {
		LOG_WRN("present backend init failed (%d); frames will drop", rc);
	}
	LOG_INF("renderer %dx%d ARGB8888 software", w, h);
	return &the_renderer;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	if (renderer == NULL) {
		return;
	}
	free(renderer->backing);
	renderer->backing = NULL;
	renderer->target = NULL;
}

int SDL_GetRendererInfo(SDL_Renderer *renderer, SDL_RendererInfo *info)
{
	ARG_UNUSED(renderer);
	memset(info, 0, sizeof(*info));
	info->name = "pizza-soft";
	info->flags = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;
	info->max_texture_width = 4096;
	info->max_texture_height = 4096;
	return 0;
}

int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
	renderer->logical_w = w;
	renderer->logical_h = h;
	if (w != renderer->out_w || h != renderer->out_h) {
		/* Present-time scaling is unimplemented; the consumers all
		 * render at the window size (HVS scales to the panel).
		 */
		LOG_WRN("logical %dx%d != output %dx%d: no present-time scaling",
			w, h, renderer->out_w, renderer->out_h);
	}
	return 0;
}

void SDL_RenderGetLogicalSize(SDL_Renderer *renderer, int *w, int *h)
{
	if (w != NULL) {
		*w = renderer->logical_w;
	}
	if (h != NULL) {
		*h = renderer->logical_h;
	}
}

int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
	if (w != NULL) {
		*w = renderer->out_w;
	}
	if (h != NULL) {
		*h = renderer->out_h;
	}
	return 0;
}

/* ── textures ────────────────────────────────────────────────── */

static int format_accepted(Uint32 format)
{
	switch (format) {
	case SDL_PIXELFORMAT_ARGB8888:
	case SDL_PIXELFORMAT_RGB888:
	case SDL_PIXELFORMAT_RGBA8888:
		return 1;
	default:
		return 0;
	}
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
	ARG_UNUSED(renderer);

	if (!format_accepted(format)) {
		s2s_set_error("CreateTexture: unsupported format %u", format);
		return NULL;
	}
	if (w <= 0 || h <= 0) {
		s2s_set_error("CreateTexture: bad dims %dx%d", w, h);
		return NULL;
	}

	SDL_Texture *t = calloc(1, sizeof(*t));

	if (t == NULL) {
		s2s_set_error("CreateTexture: out of memory");
		return NULL;
	}
	t->pixels = calloc((size_t)w * h, 4);
	if (t->pixels == NULL) {
		free(t);
		s2s_set_error("CreateTexture: out of memory (%dx%d)", w, h);
		return NULL;
	}
	t->format = format;
	t->access = access;
	t->w = w;
	t->h = h;
	t->pitch = w * 4;
	t->blend = SDL_BLENDMODE_NONE;
	return t;
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface)
{
	if (surface == NULL) {
		s2s_set_error("CreateTextureFromSurface: NULL surface");
		return NULL;
	}

	const struct s2s_surface *ps = S2S_SURF(surface);

	/* SDL semantics: the texture blends if the surface carried alpha
	 * (alpha channel, colorkey, or indexed palette alpha).
	 */
	bool has_alpha = (surface->format->Amask != 0) || ps->has_colorkey;

	if (!has_alpha && surface->format->BitsPerPixel == 8) {
		for (int i = 0; i < ps->pal.ncolors; i++) {
			if (ps->pal.colors[i].a != 255) {
				has_alpha = true;
				break;
			}
		}
	}

	SDL_Surface *conv = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);

	if (conv == NULL) {
		return NULL;
	}

	SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
					   SDL_TEXTUREACCESS_STREAMING,
					   surface->w, surface->h);
	if (t != NULL) {
		for (int y = 0; y < conv->h; y++) {
			memcpy((Uint8 *)t->pixels + (size_t)y * t->pitch,
			       (const Uint8 *)conv->pixels + (size_t)y * conv->pitch,
			       (size_t)conv->w * 4);
		}
		t->blend = has_alpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
	}
	SDL_FreeSurface(conv);
	return t;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
	if (texture == NULL) {
		return;
	}
	if (the_renderer.target == texture) {
		the_renderer.target = NULL;
	}
	free(texture->pixels);
	free(texture);
}

int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h)
{
	if (texture == NULL) {
		s2s_set_error("QueryTexture: NULL texture");
		return -1;
	}
	if (format != NULL) {
		*format = texture->format;
	}
	if (access != NULL) {
		*access = texture->access;
	}
	if (w != NULL) {
		*w = texture->w;
	}
	if (h != NULL) {
		*h = texture->h;
	}
	return 0;
}

int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode)
{
	if (texture == NULL) {
		s2s_set_error("SetTextureBlendMode: NULL texture");
		return -1;
	}
	if (blendMode != SDL_BLENDMODE_NONE && blendMode != SDL_BLENDMODE_BLEND) {
		s2s_set_error("SetTextureBlendMode: unsupported mode %d", blendMode);
		return -1;
	}
	texture->blend = blendMode;
	return 0;
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
	ARG_UNUSED(rect);

	if (texture == NULL || pixels == NULL || pitch == NULL) {
		s2s_set_error("LockTexture: NULL arg");
		return -1;
	}
	*pixels = texture->pixels;
	*pitch = texture->pitch;
	return 0;
}

void SDL_UnlockTexture(SDL_Texture *texture)
{
	ARG_UNUSED(texture);
}

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
		      const void *pixels, int pitch)
{
	if (texture == NULL || pixels == NULL) {
		s2s_set_error("UpdateTexture: NULL arg");
		return -1;
	}

	SDL_Rect r = (rect != NULL) ? *rect
				    : (SDL_Rect){ 0, 0, texture->w, texture->h };

	if (r.x < 0 || r.y < 0 || r.x + r.w > texture->w || r.y + r.h > texture->h) {
		s2s_set_error("UpdateTexture: rect out of bounds");
		return -1;
	}

	for (int y = 0; y < r.h; y++) {
		memcpy((Uint8 *)texture->pixels + (size_t)(r.y + y) * texture->pitch +
			       (size_t)r.x * 4,
		       (const Uint8 *)pixels + (size_t)y * pitch,
		       (size_t)r.w * 4);
	}
	return 0;
}

/* ── draw state + primitives ─────────────────────────────────── */

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	renderer->draw_color = ((Uint32)a << 24) | ((Uint32)r << 16) |
			       ((Uint32)g << 8) | (Uint32)b;
	return 0;
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
	uint32_t *px = target_pixels(renderer);
	size_t n = (size_t)target_w(renderer) * target_h(renderer);
	uint32_t c = renderer->draw_color;

	for (size_t i = 0; i < n; i++) {
		px[i] = c;
	}
	return 0;
}

static void put_pixel(SDL_Renderer *r, int x, int y)
{
	if (x < 0 || y < 0 || x >= target_w(r) || y >= target_h(r)) {
		return;
	}
	target_pixels(r)[(size_t)y * target_w(r) + x] = r->draw_color;
}

int SDL_RenderDrawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2)
{
	int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
	int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
	int sx = (x1 < x2) ? 1 : -1;
	int sy = (y1 < y2) ? 1 : -1;
	int err = dx - dy;

	for (;;) {
		put_pixel(renderer, x1, y1);
		if (x1 == x2 && y1 == y2) {
			break;
		}

		int e2 = 2 * err;

		if (e2 > -dy) {
			err -= dy;
			x1 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y1 += sy;
		}
	}
	return 0;
}

int SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
	SDL_Rect r = (rect != NULL) ? *rect
				    : (SDL_Rect){ 0, 0, target_w(renderer), target_h(renderer) };

	if (r.w <= 0 || r.h <= 0) {
		return 0;
	}
	SDL_RenderDrawLine(renderer, r.x, r.y, r.x + r.w - 1, r.y);
	SDL_RenderDrawLine(renderer, r.x, r.y + r.h - 1, r.x + r.w - 1, r.y + r.h - 1);
	SDL_RenderDrawLine(renderer, r.x, r.y, r.x, r.y + r.h - 1);
	SDL_RenderDrawLine(renderer, r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h - 1);
	return 0;
}

int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
	int tw = target_w(renderer);
	int th = target_h(renderer);
	SDL_Rect r = (rect != NULL) ? *rect : (SDL_Rect){ 0, 0, tw, th };

	int x0 = MAX(r.x, 0);
	int y0 = MAX(r.y, 0);
	int x1 = MIN(r.x + r.w, tw);
	int y1 = MIN(r.y + r.h, th);

	if (x0 >= x1 || y0 >= y1) {
		return 0;
	}

	uint32_t *px = target_pixels(renderer);
	uint32_t c = renderer->draw_color;

	for (int y = y0; y < y1; y++) {
		uint32_t *row = px + (size_t)y * tw;

		for (int x = x0; x < x1; x++) {
			row[x] = c;
		}
	}
	return 0;
}

/* ── RenderCopy ──────────────────────────────────────────────── */

static inline uint32_t blend_px(uint32_t src, uint32_t dst)
{
	uint32_t a = src >> 24;

	if (a == 255) {
		return src | 0xFF000000u;
	}
	if (a == 0) {
		return dst;
	}

	uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
	uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
	uint32_t r = (sr * a + dr * (255 - a)) / 255;
	uint32_t g = (sg * a + dg * (255 - a)) / 255;
	uint32_t b = (sb * a + db * (255 - a)) / 255;

	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
		   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	if (texture == NULL) {
		s2s_set_error("RenderCopy: NULL texture");
		return -1;
	}

	SDL_Rect sr = (srcrect != NULL) ? *srcrect
					: (SDL_Rect){ 0, 0, texture->w, texture->h };
	SDL_Rect dr = (dstrect != NULL) ? *dstrect
					: (SDL_Rect){ 0, 0, target_w(renderer), target_h(renderer) };

	if (sr.w <= 0 || sr.h <= 0 || dr.w <= 0 || dr.h <= 0) {
		return 0;
	}

	/* Clip src rect to the texture. */
	if (sr.x < 0) { sr.w += sr.x; sr.x = 0; }
	if (sr.y < 0) { sr.h += sr.y; sr.y = 0; }
	sr.w = MIN(sr.w, texture->w - sr.x);
	sr.h = MIN(sr.h, texture->h - sr.y);
	if (sr.w <= 0 || sr.h <= 0) {
		return 0;
	}

	int tw = target_w(renderer);
	int th = target_h(renderer);

	/* Clip dst rect to the target, tracking the offset into the
	 * destination span so scaled source stepping stays aligned.
	 */
	int dx0 = MAX(dr.x, 0);
	int dy0 = MAX(dr.y, 0);
	int dx1 = MIN(dr.x + dr.w, tw);
	int dy1 = MIN(dr.y + dr.h, th);

	if (dx0 >= dx1 || dy0 >= dy1) {
		return 0;
	}

	uint32_t *tpx = target_pixels(renderer);
	bool blend = (texture->blend == SDL_BLENDMODE_BLEND);
	bool scaled = (sr.w != dr.w) || (sr.h != dr.h);

	if (!scaled && !blend) {
		/* 1:1 opaque: row memcpy. */
		for (int y = dy0; y < dy1; y++) {
			int sy = sr.y + (y - dr.y);

			memcpy(tpx + (size_t)y * tw + dx0,
			       texture->pixels + (size_t)sy * texture->w +
				       (sr.x + (dx0 - dr.x)),
			       (size_t)(dx1 - dx0) * 4);
		}
		return 0;
	}

	/* General path: 16.16 fixed-point source stepping (nearest). */
	uint32_t xstep = ((uint32_t)sr.w << 16) / (uint32_t)dr.w;
	uint32_t ystep = ((uint32_t)sr.h << 16) / (uint32_t)dr.h;
	uint32_t syf = (uint32_t)(dy0 - dr.y) * ystep;

	for (int y = dy0; y < dy1; y++, syf += ystep) {
		const uint32_t *srow = texture->pixels +
				       (size_t)(sr.y + (int)(syf >> 16)) * texture->w + sr.x;
		uint32_t *drow = tpx + (size_t)y * tw;
		uint32_t sxf = (uint32_t)(dx0 - dr.x) * xstep;

		if (blend) {
			for (int x = dx0; x < dx1; x++, sxf += xstep) {
				drow[x] = blend_px(srow[sxf >> 16], drow[x]);
			}
		} else {
			for (int x = dx0; x < dx1; x++, sxf += xstep) {
				drow[x] = srow[sxf >> 16];
			}
		}
	}
	return 0;
}

/* ── render target + present ─────────────────────────────────── */

int SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
	if (texture != NULL && texture->access != SDL_TEXTUREACCESS_TARGET) {
		s2s_set_error("SetRenderTarget: texture not TEXTUREACCESS_TARGET");
		return -1;
	}
	renderer->target = texture;
	return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
	if (renderer->target != NULL) {
		LOG_WRN("RenderPresent with render target bound");
	}
	s2s_present_frame(renderer->backing, renderer->out_w * 4);

	if (renderer->frame_ms != 0) {
		Uint32 now = SDL_GetTicks();
		Uint32 elapsed = now - renderer->last_present;

		if (elapsed < renderer->frame_ms) {
			SDL_Delay(renderer->frame_ms - elapsed);
		}
		renderer->last_present = SDL_GetTicks();
	}
}
