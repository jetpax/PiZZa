/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- software surfaces, palettes and (phase 3) blits.
 *
 * SDLPoP's surface universe is small: 8bpp indexed sprites with a
 * 16-color palette + colorkey, one 24bpp RGB onscreen composition
 * surface, and 32bpp overlay/peel helpers. Pixel memory is always
 * CPU-visible; Lock/Unlock are no-ops.
 *
 * Phase 1: creation/bookkeeping is real, blits are stubs.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(s2s_surface, CONFIG_SDLPOP_LOG_LEVEL);

static Uint8 mask_shift(Uint32 mask)
{
	Uint8 shift = 0;

	if (mask == 0) {
		return 0;
	}
	while ((mask & 1u) == 0) {
		mask >>= 1;
		shift++;
	}
	return shift;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
				  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	ARG_UNUSED(flags);

	if (depth != 8 && depth != 24 && depth != 32) {
		s2s_set_error("CreateRGBSurface: unsupported depth %d", depth);
		return NULL;
	}

	struct s2s_surface *ps = calloc(1, sizeof(*ps));

	if (ps == NULL) {
		s2s_set_error("CreateRGBSurface: out of memory");
		return NULL;
	}

	ps->fmt.BitsPerPixel = depth;
	ps->fmt.BytesPerPixel = depth / 8;
	ps->fmt.Rmask = Rmask;
	ps->fmt.Gmask = Gmask;
	ps->fmt.Bmask = Bmask;
	ps->fmt.Amask = Amask;
	ps->fmt.Rshift = mask_shift(Rmask);
	ps->fmt.Gshift = mask_shift(Gmask);
	ps->fmt.Bshift = mask_shift(Bmask);
	ps->fmt.Ashift = mask_shift(Amask);

	if (depth == 8) {
		ps->fmt.format = SDL_PIXELFORMAT_INDEX8;
		ps->pal.ncolors = 256;
		ps->pal.colors = ps->colors;
		ps->fmt.palette = &ps->pal;
	} else if (depth == 24) {
		ps->fmt.format = SDL_PIXELFORMAT_RGB24;
	} else {
		ps->fmt.format = SDL_PIXELFORMAT_ARGB8888;
	}

	int pitch = (width * ps->fmt.BytesPerPixel + 3) & ~3;
	void *pixels = calloc((size_t)pitch, (size_t)height);

	if (pixels == NULL) {
		free(ps);
		s2s_set_error("CreateRGBSurface: out of memory (%dx%d@%d)",
			      width, height, depth);
		return NULL;
	}

	ps->s.format = &ps->fmt;
	ps->s.w = width;
	ps->s.h = height;
	ps->s.pitch = pitch;
	ps->s.pixels = pixels;
	ps->s.clip_rect = (SDL_Rect){ 0, 0, width, height };
	ps->s.refcount = 1;
	ps->alpha_mod = 255;
	ps->blend = SDL_BLENDMODE_NONE;

	return &ps->s;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
	if (surface == NULL) {
		return;
	}
	free(surface->pixels);
	free(S2S_SURF(surface));
}

int SDL_LockSurface(SDL_Surface *surface)
{
	if (surface == NULL) {
		s2s_set_error("LockSurface: NULL surface");
		return -1;
	}
	return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
	ARG_UNUSED(surface);
}

SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
	if (rect == NULL) {
		surface->clip_rect = (SDL_Rect){ 0, 0, surface->w, surface->h };
		return SDL_TRUE;
	}

	int x0 = MAX(rect->x, 0);
	int y0 = MAX(rect->y, 0);
	int x1 = MIN(rect->x + rect->w, surface->w);
	int y1 = MIN(rect->y + rect->h, surface->h);

	surface->clip_rect = (SDL_Rect){ x0, y0, MAX(x1 - x0, 0), MAX(y1 - y0, 0) };
	return (surface->clip_rect.w > 0 && surface->clip_rect.h > 0) ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
	if (surface == NULL) {
		s2s_set_error("SetColorKey: NULL surface");
		return -1;
	}
	S2S_SURF(surface)->has_colorkey = (flag != 0);
	S2S_SURF(surface)->colorkey = key;
	return 0;
}

int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha)
{
	if (surface == NULL) {
		s2s_set_error("SetSurfaceAlphaMod: NULL surface");
		return -1;
	}
	S2S_SURF(surface)->alpha_mod = alpha;
	return 0;
}

int SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode blendMode)
{
	if (surface == NULL) {
		s2s_set_error("SetSurfaceBlendMode: NULL surface");
		return -1;
	}
	S2S_SURF(surface)->blend = blendMode;
	return 0;
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
			 int firstcolor, int ncolors)
{
	if (palette == NULL || colors == NULL) {
		s2s_set_error("SetPaletteColors: NULL palette");
		return -1;
	}
	if (firstcolor < 0 || firstcolor + ncolors > palette->ncolors) {
		s2s_set_error("SetPaletteColors: range %d+%d out of %d",
			      firstcolor, ncolors, palette->ncolors);
		return -1;
	}
	memcpy(&palette->colors[firstcolor], colors, (size_t)ncolors * sizeof(SDL_Color));
	return 0;
}

int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
	if (surface == NULL || palette == NULL) {
		s2s_set_error("SetSurfacePalette: NULL arg");
		return -1;
	}

	struct s2s_surface *ps = S2S_SURF(surface);

	if (surface->format->BitsPerPixel != 8) {
		s2s_set_error("SetSurfacePalette: not an indexed surface");
		return -1;
	}

	int n = MIN(palette->ncolors, ps->pal.ncolors);

	memcpy(ps->colors, palette->colors, (size_t)n * sizeof(SDL_Color));
	return 0;
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (format->BitsPerPixel == 8) {
		/* Nearest palette entry (exact match in practice: the game
		 * maps colors it previously set).
		 */
		const SDL_Palette *pal = format->palette;
		int best = 0;
		unsigned int best_d = UINT32_MAX;

		for (int i = 0; i < pal->ncolors; i++) {
			int dr = (int)pal->colors[i].r - r;
			int dg = (int)pal->colors[i].g - g;
			int db = (int)pal->colors[i].b - b;
			unsigned int d = (unsigned int)(dr * dr + dg * dg + db * db);

			if (d < best_d) {
				best_d = d;
				best = i;
				if (d == 0) {
					break;
				}
			}
		}
		return (Uint32)best;
	}

	Uint32 v = ((Uint32)r << format->Rshift) |
		   ((Uint32)g << format->Gshift) |
		   ((Uint32)b << format->Bshift);

	if (format->Amask != 0) {
		v |= (Uint32)a << format->Ashift;
	}
	return v;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
	return SDL_MapRGBA(format, r, g, b, SDL_ALPHA_OPAQUE);
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
	if (dst == NULL) {
		s2s_set_error("FillRect: NULL surface");
		return -1;
	}

	SDL_Rect area = (rect != NULL) ? *rect : (SDL_Rect){ 0, 0, dst->w, dst->h };

	/* clip to surface + clip_rect */
	int x0 = MAX(MAX(area.x, dst->clip_rect.x), 0);
	int y0 = MAX(MAX(area.y, dst->clip_rect.y), 0);
	int x1 = MIN(MIN(area.x + area.w, dst->clip_rect.x + dst->clip_rect.w), dst->w);
	int y1 = MIN(MIN(area.y + area.h, dst->clip_rect.y + dst->clip_rect.h), dst->h);

	if (x0 >= x1 || y0 >= y1) {
		return 0;
	}

	int bpp = dst->format->BytesPerPixel;
	Uint8 *row = (Uint8 *)dst->pixels + (size_t)y0 * dst->pitch + (size_t)x0 * bpp;

	for (int y = y0; y < y1; y++) {
		Uint8 *px = row;

		switch (bpp) {
		case 1:
			memset(px, (int)(color & 0xFF), (size_t)(x1 - x0));
			break;
		case 3:
			for (int x = x0; x < x1; x++) {
				px[0] = (Uint8)(color);
				px[1] = (Uint8)(color >> 8);
				px[2] = (Uint8)(color >> 16);
				px += 3;
			}
			break;
		case 4:
			for (int x = x0; x < x1; x++) {
				*(Uint32 *)px = color;
				px += 4;
			}
			break;
		}
		row += dst->pitch;
	}
	return 0;
}

/* ── blits ───────────────────────────────────────────────────── */

/* Read one source pixel as RGBA + "skip" (colorkeyed under
 * BLENDMODE_NONE, SDL semantics).
 */
static inline bool read_px(const struct s2s_surface *ps, const Uint8 *p,
			   Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
	switch (ps->fmt.BytesPerPixel) {
	case 1: {
		Uint8 idx = *p;

		if (ps->has_colorkey && idx == (Uint8)ps->colorkey) {
			return false;
		}
		const SDL_Color *c = &ps->pal.colors[idx];

		*r = c->r;
		*g = c->g;
		*b = c->b;
		*a = 255;
		return true;
	}
	case 3:
		if (ps->has_colorkey &&
		    (Uint32)(p[0] | (p[1] << 8) | (p[2] << 16)) == ps->colorkey) {
			return false;
		}
		*r = p[0];
		*g = p[1];
		*b = p[2];
		*a = 255;
		return true;
	default: {
		Uint32 v = *(const Uint32 *)p;

		if (ps->has_colorkey && (v & 0x00FFFFFF) == (ps->colorkey & 0x00FFFFFF)) {
			return false;
		}
		*r = (Uint8)(v >> ps->fmt.Rshift);
		*g = (Uint8)(v >> ps->fmt.Gshift);
		*b = (Uint8)(v >> ps->fmt.Bshift);
		*a = (ps->fmt.Amask != 0) ? (Uint8)(v >> ps->fmt.Ashift) : 255;
		return true;
	}
	}
}

static inline void write_px(const struct s2s_surface *pd, Uint8 *p,
			    Uint8 r, Uint8 g, Uint8 b, Uint8 a, SDL_BlendMode blend)
{
	if (blend == SDL_BLENDMODE_BLEND) {
		if (a == 0) {
			return;
		}
		if (a != 255) {
			Uint8 dr, dg, db;

			switch (pd->fmt.BytesPerPixel) {
			case 3:
				dr = p[0];
				dg = p[1];
				db = p[2];
				break;
			case 4: {
				Uint32 v = *(Uint32 *)p;

				dr = (Uint8)(v >> pd->fmt.Rshift);
				dg = (Uint8)(v >> pd->fmt.Gshift);
				db = (Uint8)(v >> pd->fmt.Bshift);
				break;
			}
			default:
				return;   /* blend onto 8bpp unsupported */
			}
			r = (Uint8)((r * a + dr * (255 - a)) / 255);
			g = (Uint8)((g * a + dg * (255 - a)) / 255);
			b = (Uint8)((b * a + db * (255 - a)) / 255);
			a = 255;
		}
	}

	switch (pd->fmt.BytesPerPixel) {
	case 1:
		*p = (Uint8)SDL_MapRGB(&((struct s2s_surface *)pd)->fmt, r, g, b);
		break;
	case 3:
		p[0] = r;
		p[1] = g;
		p[2] = b;
		break;
	default:
		*(Uint32 *)p = ((Uint32)r << pd->fmt.Rshift) |
			       ((Uint32)g << pd->fmt.Gshift) |
			       ((Uint32)b << pd->fmt.Bshift) |
			       ((pd->fmt.Amask != 0) ? ((Uint32)a << pd->fmt.Ashift) : 0);
		break;
	}
}

int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
		    SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == NULL || dst == NULL) {
		s2s_set_error("BlitSurface: NULL surface");
		return -1;
	}

	const struct s2s_surface *ps = S2S_SURF(src);
	const struct s2s_surface *pd = S2S_SURF(dst);

	SDL_Rect sr = (srcrect != NULL) ? *srcrect : (SDL_Rect){ 0, 0, src->w, src->h };
	int dx = (dstrect != NULL) ? dstrect->x : 0;
	int dy = (dstrect != NULL) ? dstrect->y : 0;

	/* clip source rect to source bounds */
	if (sr.x < 0) { sr.w += sr.x; dx -= sr.x; sr.x = 0; }
	if (sr.y < 0) { sr.h += sr.y; dy -= sr.y; sr.y = 0; }
	sr.w = MIN(sr.w, src->w - sr.x);
	sr.h = MIN(sr.h, src->h - sr.y);

	/* clip against dst clip_rect */
	int cx0 = dst->clip_rect.x, cy0 = dst->clip_rect.y;
	int cx1 = cx0 + dst->clip_rect.w, cy1 = cy0 + dst->clip_rect.h;

	if (dx < cx0) { sr.x += cx0 - dx; sr.w -= cx0 - dx; dx = cx0; }
	if (dy < cy0) { sr.y += cy0 - dy; sr.h -= cy0 - dy; dy = cy0; }
	sr.w = MIN(sr.w, cx1 - dx);
	sr.h = MIN(sr.h, cy1 - dy);

	if (sr.w <= 0 || sr.h <= 0) {
		return 0;
	}

	SDL_BlendMode blend = ps->blend;
	int sbpp = ps->fmt.BytesPerPixel;
	int dbpp = pd->fmt.BytesPerPixel;

	/* fast path: same layout, no colorkey, no blending */
	if (blend != SDL_BLENDMODE_BLEND && !ps->has_colorkey && sbpp == dbpp &&
	    (sbpp != 1 || memcmp(ps->pal.colors, pd->pal.colors,
				 sizeof(ps->pal.colors)) == 0)) {
		for (int y = 0; y < sr.h; y++) {
			memcpy((Uint8 *)dst->pixels + (size_t)(dy + y) * dst->pitch +
				       (size_t)dx * dbpp,
			       (const Uint8 *)src->pixels + (size_t)(sr.y + y) * src->pitch +
				       (size_t)sr.x * sbpp,
			       (size_t)sr.w * sbpp);
		}
		return 0;
	}

	/* 8bpp -> 8bpp with colorkey: index copy (palettes match in
	 * practice -- both sides come from the same game palette).
	 */
	if (sbpp == 1 && dbpp == 1) {
		for (int y = 0; y < sr.h; y++) {
			const Uint8 *sp = (const Uint8 *)src->pixels +
					  (size_t)(sr.y + y) * src->pitch + sr.x;
			Uint8 *dp = (Uint8 *)dst->pixels +
				    (size_t)(dy + y) * dst->pitch + dx;

			for (int x = 0; x < sr.w; x++) {
				if (!ps->has_colorkey || sp[x] != (Uint8)ps->colorkey) {
					dp[x] = sp[x];
				}
			}
		}
		return 0;
	}

	Uint8 alpha_mod = ps->alpha_mod;

	for (int y = 0; y < sr.h; y++) {
		const Uint8 *sp = (const Uint8 *)src->pixels +
				  (size_t)(sr.y + y) * src->pitch + (size_t)sr.x * sbpp;
		Uint8 *dp = (Uint8 *)dst->pixels +
			    (size_t)(dy + y) * dst->pitch + (size_t)dx * dbpp;

		for (int x = 0; x < sr.w; x++) {
			Uint8 r, g, b, a;

			if (read_px(ps, sp, &r, &g, &b, &a)) {
				if (blend == SDL_BLENDMODE_BLEND && alpha_mod != 255) {
					a = (Uint8)((a * alpha_mod) / 255);
				}
				write_px(pd, dp, r, g, b, a, blend);
			}
			sp += sbpp;
			dp += dbpp;
		}
	}
	return 0;
}

int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
		   SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == NULL || dst == NULL) {
		s2s_set_error("BlitScaled: NULL surface");
		return -1;
	}

	const struct s2s_surface *ps = S2S_SURF(src);
	const struct s2s_surface *pd = S2S_SURF(dst);

	SDL_Rect sr = (srcrect != NULL) ? *srcrect : (SDL_Rect){ 0, 0, src->w, src->h };
	SDL_Rect dr = (dstrect != NULL) ? *dstrect : (SDL_Rect){ 0, 0, dst->w, dst->h };

	if (sr.w <= 0 || sr.h <= 0 || dr.w <= 0 || dr.h <= 0) {
		return 0;
	}

	int sbpp = ps->fmt.BytesPerPixel;
	int dbpp = pd->fmt.BytesPerPixel;

	for (int y = 0; y < dr.h; y++) {
		int sy = sr.y + (int)(((int64_t)y * sr.h) / dr.h);

		if (dr.y + y < 0 || dr.y + y >= dst->h) {
			continue;
		}
		const Uint8 *srow = (const Uint8 *)src->pixels + (size_t)sy * src->pitch;
		Uint8 *dp = (Uint8 *)dst->pixels + (size_t)(dr.y + y) * dst->pitch +
			    (size_t)dr.x * dbpp;

		for (int x = 0; x < dr.w; x++) {
			if (dr.x + x < 0 || dr.x + x >= dst->w) {
				dp += dbpp;
				continue;
			}
			int sx = sr.x + (int)(((int64_t)x * sr.w) / dr.w);
			Uint8 r, g, b, a;

			if (read_px(ps, srow + (size_t)sx * sbpp, &r, &g, &b, &a)) {
				write_px(pd, dp, r, g, b, a, ps->blend);
			}
			dp += dbpp;
		}
	}
	return 0;
}

SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags)
{
	ARG_UNUSED(flags);

	if (src == NULL) {
		s2s_set_error("ConvertSurfaceFormat: NULL surface");
		return NULL;
	}

	SDL_Surface *out;

	switch (pixel_format) {
	case SDL_PIXELFORMAT_ARGB8888:
		out = SDL_CreateRGBSurface(0, src->w, src->h, 32,
					   0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
		break;
	case SDL_PIXELFORMAT_RGB24:
		out = SDL_CreateRGBSurface(0, src->w, src->h, 24,
					   0x000000FF, 0x0000FF00, 0x00FF0000, 0);
		break;
	default:
		s2s_set_error("ConvertSurfaceFormat: unsupported target %u", pixel_format);
		return NULL;
	}
	if (out == NULL) {
		return NULL;
	}

	const struct s2s_surface *ps = S2S_SURF(src);
	const struct s2s_surface *po = S2S_SURF(out);
	int sbpp = ps->fmt.BytesPerPixel;
	int obpp = po->fmt.BytesPerPixel;

	for (int y = 0; y < src->h; y++) {
		const Uint8 *sp = (const Uint8 *)src->pixels + (size_t)y * src->pitch;
		Uint8 *dp = (Uint8 *)out->pixels + (size_t)y * out->pitch;

		for (int x = 0; x < src->w; x++) {
			Uint8 r = 0, g = 0, b = 0, a = 0;

			/* SDL semantics: a colorkey (and indexed palette
			 * alpha) becomes real alpha in the converted
			 * surface.
			 */
			if (read_px(ps, sp, &r, &g, &b, &a)) {
				if (sbpp == 1) {
					a = ps->pal.colors[*sp].a;
				}
			} else {
				a = 0;
			}
			write_px(po, dp, r, g, b, a, SDL_BLENDMODE_NONE);
			sp += sbpp;
			dp += obpp;
		}
	}
	return out;
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags)
{
	ARG_UNUSED(flags);

	if (src == NULL || fmt == NULL) {
		s2s_set_error("ConvertSurface: NULL arg");
		return NULL;
	}

	/* Same-format duplicate (SDLPoP's hflip does this to clone an
	 * indexed sprite). ConvertSurfaceFormat only targets truecolor,
	 * so handle the indexed/identical case here directly.
	 */
	if (fmt->format == src->format->format) {
		SDL_Surface *out = SDL_CreateRGBSurface(0, src->w, src->h,
			src->format->BitsPerPixel,
			src->format->Rmask, src->format->Gmask,
			src->format->Bmask, src->format->Amask);

		if (out == NULL) {
			return NULL;
		}
		for (int y = 0; y < src->h; y++) {
			memcpy((Uint8 *)out->pixels + (size_t)y * out->pitch,
			       (const Uint8 *)src->pixels + (size_t)y * src->pitch,
			       (size_t)src->w * src->format->BytesPerPixel);
		}
		if (src->format->BitsPerPixel == 8) {
			memcpy(S2S_SURF(out)->colors, S2S_SURF(src)->colors,
			       sizeof(S2S_SURF(out)->colors));
		}
		struct s2s_surface *po = S2S_SURF(out);
		struct s2s_surface *psrc = S2S_SURF(src);

		po->has_colorkey = psrc->has_colorkey;
		po->colorkey = psrc->colorkey;
		po->alpha_mod = psrc->alpha_mod;
		po->blend = psrc->blend;
		return out;
	}

	return SDL_ConvertSurfaceFormat(src, fmt->format, flags);
}
