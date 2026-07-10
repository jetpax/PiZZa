/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Mini vMac -- renderer/texture and the present hand-off.
 *
 * Mini vMac's OSGLUSDL.c drives a streaming SDL texture: it locks the
 * 512x342 ARGB8888 texture (SDL_LockTexture), writes the 1-bit Mac screen
 * expanded to ARGB through its own CLUT/ScrnMapr, unlocks it, then
 * SDL_RenderCopy + SDL_RenderPresent. The shim keeps the texture as a plain
 * CPU buffer; RenderPresent pushes it through the sdl2shim present seam
 * (minivmac_present_*). The VC4 HVS owns any scaling to the panel; the shim
 * scales nothing. App-local, like Doom's shim_video.c -- the Mac diverges by
 * frame format (512x342 ARGB via LockTexture vs Doom's 320x200 XRGB via
 * UpdateTexture).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(minivmac_video, CONFIG_SDL2SHIM_LOG_LEVEL);

#define MINIVMAC_W 512
#define MINIVMAC_H 342
#define MAX_TEXTURES 2

struct SDL_Texture {
	Uint32 format;
	int access;
	int w, h;
	int pitch;
	Uint8 *pixels;
	bool in_use;
};

struct SDL_Renderer {
	int logical_w, logical_h;
	SDL_Texture *present_src;
	bool present_ready;
};

static struct SDL_Renderer the_renderer;
static struct SDL_Texture textures[MAX_TEXTURES];

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	ARG_UNUSED(window);
	ARG_UNUSED(index);
	ARG_UNUSED(flags);

	the_renderer.logical_w = MINIVMAC_W;
	the_renderer.logical_h = MINIVMAC_H;

	int rc = s2s_present_init(MINIVMAC_W, MINIVMAC_H);

	if (rc != 0) {
		LOG_WRN("present backend init failed (%d); frames will drop", rc);
	}
	return &the_renderer;
}

int SDL_GetRendererInfo(SDL_Renderer *renderer, SDL_RendererInfo *info)
{
	ARG_UNUSED(renderer);
	memset(info, 0, sizeof(*info));
	info->name = "pizza-fb";
	info->flags = SDL_RENDERER_SOFTWARE;
	info->max_texture_width = 1024;
	info->max_texture_height = 1024;
	return 0;
}

static int fmt_bpp(Uint32 format)
{
	switch (format) {
	case SDL_PIXELFORMAT_RGB24:
		return 3;
	case SDL_PIXELFORMAT_RGB888:
	case SDL_PIXELFORMAT_ARGB8888:
		return 4;
	default:
		return 0;
	}
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
	ARG_UNUSED(renderer);

	int bpp = fmt_bpp(format);

	if (bpp == 0) {
		s2s_set_error("CreateTexture: unsupported format %u", format);
		return NULL;
	}

	for (int i = 0; i < MAX_TEXTURES; i++) {
		if (textures[i].in_use) {
			continue;
		}

		struct SDL_Texture *t = &textures[i];

		t->pitch = w * bpp;
		t->pixels = calloc((size_t)t->pitch, (size_t)h);
		if (t->pixels == NULL) {
			s2s_set_error("CreateTexture: out of memory (%dx%d)", w, h);
			return NULL;
		}
		t->format = format;
		t->access = access;
		t->w = w;
		t->h = h;
		t->in_use = true;
		LOG_INF("texture %d: %dx%d fmt %u access %d", i, w, h, format, access);
		return t;
	}

	s2s_set_error("CreateTexture: pool exhausted");
	return NULL;
}

/* Streaming-texture present: LockTexture hands back the texture's own CPU
 * buffer to write into; UnlockTexture is a no-op since that buffer *is* the
 * texture. OSGLUSDL passes rect == NULL (whole texture).
 */
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

int SDL_RenderClear(SDL_Renderer *renderer)
{
	ARG_UNUSED(renderer);
	/* RenderCopy always covers the full output; nothing to clear. */
	return 0;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
		   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	ARG_UNUSED(srcrect);
	ARG_UNUSED(dstrect);
	if (texture == NULL) {
		s2s_set_error("RenderCopy: NULL texture");
		return -1;
	}
	renderer->present_src = texture;
	renderer->present_ready = true;
	return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
	SDL_Texture *t = renderer->present_src;

	if (!renderer->present_ready || t == NULL) {
		return;
	}
	renderer->present_ready = false;

	s2s_present_frame(t->pixels, t->pitch);
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
	if (texture == NULL) {
		return;
	}
	if (the_renderer.present_src == texture) {
		the_renderer.present_src = NULL;
		the_renderer.present_ready = false;
	}
	free(texture->pixels);
	texture->pixels = NULL;
	texture->in_use = false;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	ARG_UNUSED(renderer);
	the_renderer.present_src = NULL;
	the_renderer.present_ready = false;
}

/* ── pixel format: Mini vMac's 1bpp->ARGB CLUT build ──────────────── */

/* OSGLUSDL calls SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888) once, then
 * SDL_MapRGB per CLUT entry (black + white for a 1-bit Mac screen). One
 * static format suffices; FreeFormat is a no-op. App-local because the CLUT
 * is part of the Mac's present path, and the lib has no SDL_MapRGB today.
 */
static SDL_PixelFormat argb8888_format = {
	.format = SDL_PIXELFORMAT_ARGB8888,
	.palette = NULL,
	.BitsPerPixel = 32,
	.BytesPerPixel = 4,
	.Rmask = 0x00FF0000, .Gmask = 0x0000FF00,
	.Bmask = 0x000000FF, .Amask = 0xFF000000,
	.Rshift = 16, .Gshift = 8, .Bshift = 0, .Ashift = 24,
};

SDL_PixelFormat *SDL_AllocFormat(Uint32 pixel_format)
{
	if (pixel_format != SDL_PIXELFORMAT_ARGB8888) {
		s2s_set_error("AllocFormat: unsupported format %u", pixel_format);
		return NULL;
	}
	return &argb8888_format;
}

void SDL_FreeFormat(SDL_PixelFormat *format)
{
	ARG_UNUSED(format);
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
	return format->Amask
	       | ((Uint32)r << format->Rshift)
	       | ((Uint32)g << format->Gshift)
	       | ((Uint32)b << format->Bshift);
}
