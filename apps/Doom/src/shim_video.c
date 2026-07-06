/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa sdl2-doom -- renderer/texture and the present hand-off.
 *
 * Doom's i_video.c (build-time patched to render at native 320x200 --
 * see CMakeLists, SDL_RESX/SDL_RESY) fills one 320x200 XRGB8888 texture
 * (SDL_PIXELFORMAT_RGB888) per frame via a single SDL_UpdateTexture,
 * then SDL_RenderCopy + SDL_RenderPresent. The shim keeps textures as
 * plain CPU buffers and RenderPresent pushes the copied buffer through
 * the sdl2shim present seam (doom_present_*). The VC4 HVS owns any
 * scaling to the panel; the shim scales nothing.
 *
 * This is the one piece of the shim that diverges from SDLPoP (which
 * presents 320x200 RGB24). It stays app-local until the shared lib
 * grows a format-general present seam.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(doom_video, CONFIG_SDL2SHIM_LOG_LEVEL);

#define DOOM_W 320
#define DOOM_H 200
#define MAX_TEXTURES 4

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

	the_renderer.logical_w = DOOM_W;
	the_renderer.logical_h = DOOM_H;

	int rc = s2s_present_init(DOOM_W, DOOM_H);

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

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
		      const void *pixels, int pitch)
{
	if (texture == NULL || pixels == NULL) {
		s2s_set_error("UpdateTexture: NULL texture/pixels");
		return -1;
	}
	/* Doom updates the whole texture each frame (rect == NULL). */
	ARG_UNUSED(rect);

	const Uint8 *src = pixels;
	Uint8 *dst = texture->pixels;
	int row_bytes = MIN(pitch, texture->pitch);

	for (int y = 0; y < texture->h; y++) {
		memcpy(dst, src, (size_t)row_bytes);
		src += pitch;
		dst += texture->pitch;
	}
	return 0;
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

	if (t->w != DOOM_W || t->h != DOOM_H) {
		/* Only the native 320x200 path is wired. */
		return;
	}
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
