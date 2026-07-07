/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- renderer/texture surface and the present hand-off.
 *
 * SDLPoP's default ("sharp") present path is:
 *   SDL_UpdateTexture(texture_sharp <- onscreen 320x200 RGB24)
 *   SDL_RenderCopy(renderer, texture_sharp, NULL, NULL)
 *   SDL_RenderPresent(renderer)
 * The shim keeps textures as plain CPU buffers, remembers the last
 * texture copied, and RenderPresent pushes that buffer through the
 * pop_present_* seam (bcm2835_fb + HVS scaling on hardware; capture
 * or /dev/null in sim). No renderer-side scaling is done: HVS owns
 * scaling, SDL_RenderSetLogicalSize / SetIntegerScale just record
 * intent.
 *
 * The renderer does NOT advertise SDL_RENDERER_TARGETTEXTURE, which
 * keeps SDLPoP off the render-to-texture ("fuzzy") path.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(pop_video, CONFIG_SDLPOP_LOG_LEVEL);

#define POP_MAX_TEXTURES 6

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
	SDL_bool integer_scale;
	SDL_Texture *present_src;
	bool present_ready;
};

static struct SDL_Renderer the_renderer;
static struct SDL_Texture textures[POP_MAX_TEXTURES];

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	ARG_UNUSED(window);
	ARG_UNUSED(index);
	ARG_UNUSED(flags);

	the_renderer.logical_w = 320;
	the_renderer.logical_h = 200;

	int rc = s2s_present_init(320, 200);

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

int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
	if (w != NULL) {
		*w = renderer->logical_w;
	}
	if (h != NULL) {
		*h = renderer->logical_h;
	}
	return 0;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
	ARG_UNUSED(renderer);

	int bpp;

	switch (format) {
	case SDL_PIXELFORMAT_RGB24:
		bpp = 3;
		break;
	case SDL_PIXELFORMAT_ARGB8888:
		bpp = 4;
		break;
	default:
		s2s_set_error("CreateTexture: unsupported format %u", format);
		return NULL;
	}

	for (int i = 0; i < POP_MAX_TEXTURES; i++) {
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
	if (rect != NULL) {
		s2s_set_error("UpdateTexture: partial updates unsupported");
		return -1;
	}

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

	if (t->format != SDL_PIXELFORMAT_RGB24 || t->w != 320 || t->h != 200) {
		/* Only the sharp 320x200 path is wired; anything else is a
		 * scaling mode we deliberately don't support.
		 */
		return;
	}
	s2s_present_frame(t->pixels, t->pitch);
}

int SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
	ARG_UNUSED(renderer);
	ARG_UNUSED(texture);
	/* TARGETTEXTURE is not advertised; SDLPoP never takes this path. */
	s2s_set_error("SetRenderTarget: unsupported");
	return -1;
}

int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
	renderer->logical_w = w;
	renderer->logical_h = h;
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

int SDL_RenderSetIntegerScale(SDL_Renderer *renderer, SDL_bool enable)
{
	renderer->integer_scale = enable;
	return 0;
}

void SDL_RenderGetScale(SDL_Renderer *renderer, float *scaleX, float *scaleY)
{
	ARG_UNUSED(renderer);
	if (scaleX != NULL) {
		*scaleX = 1.0f;
	}
	if (scaleY != NULL) {
		*scaleY = 1.0f;
	}
}

void SDL_RenderGetViewport(SDL_Renderer *renderer, SDL_Rect *rect)
{
	if (rect != NULL) {
		*rect = (SDL_Rect){ 0, 0, renderer->logical_w, renderer->logical_h };
	}
}
