/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host unit tests for the dual-target shim TUs: the SDL_Renderer
 * engine (shim_render.c), surfaces (shim_surface.c), the S2SA TTF
 * rasteriser (shim_ttf_atlas.c) and the PIMG decoder
 * (shim_image_pimg.c). Same idea as SDLPoP's tests/audio_host: plain
 * cc + a tiny harness supplying the seams (error buffer, window,
 * present capture, asset store), exact-value pixel checks -- no SDL,
 * no golden files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include "sdl2shim.h"
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <sdl2shim_atlas.h>

/* ── seams the Zephyr side gets from shim_core / the app ─────── */

static char error_buf[192];

void s2s_set_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(error_buf, sizeof(error_buf), fmt, ap);
	va_end(ap);
}

const char *SDL_GetError(void)
{
	return error_buf;
}

/* RenderPresent's PRESENTVSYNC pacing references the timer API; the
 * tests create the renderer without the flag, so these are inert.
 */
static Uint32 fake_ticks;

Uint32 SDL_GetTicks(void)
{
	return fake_ticks;
}

void SDL_Delay(Uint32 ms)
{
	fake_ticks += ms;
}

struct SDL_Window {
	int w, h;
};

static struct SDL_Window the_window;

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	(void)title; (void)x; (void)y; (void)flags;
	the_window.w = w;
	the_window.h = h;
	return &the_window;
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
	if (w != NULL) {
		*w = window->w;
	}
	if (h != NULL) {
		*h = window->h;
	}
}

static const void *present_last_pixels;
static int present_last_pitch;
static int present_frames;
static int present_w, present_h;

int s2s_present_init(int width, int height)
{
	present_w = width;
	present_h = height;
	return 0;
}

void s2s_present_frame(const void *pixels, int pitch)
{
	present_last_pixels = pixels;
	present_last_pitch = pitch;
	present_frames++;
}

/* Asset store: a tiny table the tests populate. */
#define MAX_ASSETS 8
static struct {
	const char *path;
	const void *data;
	size_t size;
} assets[MAX_ASSETS];
static int n_assets;

static void asset_add(const char *path, const void *data, size_t size)
{
	assets[n_assets].path = path;
	assets[n_assets].data = data;
	assets[n_assets].size = size;
	n_assets++;
}

const void *s2s_asset_find(const char *path, size_t *size)
{
	for (int i = 0; i < n_assets; i++) {
		if (strcmp(assets[i].path, path) == 0) {
			if (size != NULL) {
				*size = assets[i].size;
			}
			return assets[i].data;
		}
	}
	return NULL;
}

/* ── check plumbing ──────────────────────────────────────────── */

static int failures;

#define CHECK(cond, ...)                                              \
	do {                                                          \
		if (!(cond)) {                                        \
			failures++;                                   \
			fprintf(stderr, "FAIL %s:%d: ", __func__, __LINE__); \
			fprintf(stderr, __VA_ARGS__);                 \
			fputc('\n', stderr);                          \
		}                                                     \
	} while (0)

static uint32_t px_at(SDL_Renderer *r, int x, int y)
{
	(void)r;
	const uint32_t *b = present_last_pixels;

	return b[(size_t)y * present_w + x];
}

/* dump the backing as PPM for eyeballing when a test fails */
static void dump_ppm(const char *name)
{
	FILE *f = fopen(name, "wb");

	if (f == NULL) {
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", present_w, present_h);
	const uint32_t *b = present_last_pixels;

	for (int i = 0; i < present_w * present_h; i++) {
		uint8_t rgb[3] = { (uint8_t)(b[i] >> 16), (uint8_t)(b[i] >> 8),
				   (uint8_t)b[i] };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

/* ── tests ───────────────────────────────────────────────────── */

static SDL_Renderer *g_ren;

static void test_clear_and_fill(void)
{
	SDL_SetRenderDrawColor(g_ren, 10, 20, 30, 255);
	SDL_RenderClear(g_ren);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 0, 0) == 0xFF0A141Eu, "clear TL = %08x", px_at(g_ren, 0, 0));
	CHECK(px_at(g_ren, 639, 479) == 0xFF0A141Eu, "clear BR");

	SDL_SetRenderDrawColor(g_ren, 255, 0, 0, 255);
	SDL_Rect r = { 10, 10, 5, 4 };

	SDL_RenderFillRect(g_ren, &r);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 10, 10) == 0xFFFF0000u, "fill TL");
	CHECK(px_at(g_ren, 14, 13) == 0xFFFF0000u, "fill BR inclusive");
	CHECK(px_at(g_ren, 15, 10) == 0xFF0A141Eu, "fill x-exclusive");
	CHECK(px_at(g_ren, 10, 14) == 0xFF0A141Eu, "fill y-exclusive");

	/* clipped fill: partially off-target */
	SDL_Rect off = { -3, -3, 6, 6 };

	SDL_RenderFillRect(g_ren, &off);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 0, 0) == 0xFFFF0000u, "clipped fill lands");
	CHECK(px_at(g_ren, 3, 3) == 0xFF0A141Eu, "clipped fill stops at 3,3");
}

static void test_lines_and_rect_outline(void)
{
	SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
	SDL_RenderClear(g_ren);
	SDL_SetRenderDrawColor(g_ren, 0, 255, 0, 255);
	SDL_RenderDrawLine(g_ren, 5, 5, 9, 9);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 5, 5) == 0xFF00FF00u, "line start");
	CHECK(px_at(g_ren, 9, 9) == 0xFF00FF00u, "line end inclusive");
	CHECK(px_at(g_ren, 7, 7) == 0xFF00FF00u, "line middle");
	CHECK(px_at(g_ren, 10, 10) == 0xFF000000u, "line stops");

	SDL_Rect r = { 20, 20, 4, 3 };

	SDL_RenderDrawRect(g_ren, &r);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 20, 20) == 0xFF00FF00u, "rect corner TL");
	CHECK(px_at(g_ren, 23, 22) == 0xFF00FF00u, "rect corner BR");
	CHECK(px_at(g_ren, 21, 21) == 0xFF000000u, "rect hollow");
}

static void test_texture_copy_and_blend(void)
{
	SDL_SetRenderDrawColor(g_ren, 0, 0, 255, 255);
	SDL_RenderClear(g_ren);

	/* opaque 1:1 with negative-dst clipping */
	SDL_Texture *t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
					   SDL_TEXTUREACCESS_STREAMING, 4, 4);
	uint32_t tp[16];

	for (int i = 0; i < 16; i++) {
		tp[i] = 0xFFFFFFFFu;
	}
	SDL_UpdateTexture(t, NULL, tp, 4 * 4);

	SDL_Rect d = { -2, -2, 4, 4 };

	SDL_RenderCopy(g_ren, t, NULL, &d);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 0, 0) == 0xFFFFFFFFu, "clipped copy lands");
	CHECK(px_at(g_ren, 2, 2) == 0xFF0000FFu, "clipped copy stops");

	/* blended copy: a=128 red over blue -> exact expected value */
	SDL_Texture *tb = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
					    SDL_TEXTUREACCESS_STREAMING, 2, 2);
	uint32_t bp[4] = { 0x80FF0000u, 0x80FF0000u, 0x80FF0000u, 0x80FF0000u };

	SDL_UpdateTexture(tb, NULL, bp, 2 * 4);
	SDL_SetTextureBlendMode(tb, SDL_BLENDMODE_BLEND);

	SDL_Rect db = { 100, 100, 2, 2 };

	SDL_RenderCopy(g_ren, tb, NULL, &db);
	SDL_RenderPresent(g_ren);
	/* r = 255*128/255 = 128; b = 255*127/255 = 127 */
	CHECK(px_at(g_ren, 100, 100) == 0xFF80007Fu,
	      "blend = %08x (want FF80007F)", px_at(g_ren, 100, 100));

	SDL_DestroyTexture(t);
	SDL_DestroyTexture(tb);
}

static void test_scaled_copy(void)
{
	SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
	SDL_RenderClear(g_ren);

	SDL_Texture *t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
					   SDL_TEXTUREACCESS_STREAMING, 2, 2);
	uint32_t tp[4] = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu };

	SDL_UpdateTexture(t, NULL, tp, 2 * 4);

	SDL_Rect d = { 0, 0, 4, 4 };

	SDL_RenderCopy(g_ren, t, NULL, &d);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 0, 0) == 0xFFFF0000u, "scale TL");
	CHECK(px_at(g_ren, 1, 1) == 0xFFFF0000u, "scale TL dup");
	CHECK(px_at(g_ren, 2, 0) == 0xFF00FF00u, "scale TR");
	CHECK(px_at(g_ren, 0, 2) == 0xFF0000FFu, "scale BL");
	CHECK(px_at(g_ren, 3, 3) == 0xFFFFFFFFu, "scale BR");

	SDL_DestroyTexture(t);
}

static void test_render_target(void)
{
	SDL_Texture *map = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGBA8888,
					     SDL_TEXTUREACCESS_TARGET, 8, 8);

	CHECK(map != NULL, "target texture created");
	CHECK(SDL_SetRenderTarget(g_ren, map) == 0, "set target");
	SDL_SetRenderDrawColor(g_ren, 200, 100, 50, 255);
	SDL_RenderClear(g_ren);
	CHECK(SDL_SetRenderTarget(g_ren, NULL) == 0, "unset target");

	SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
	SDL_RenderClear(g_ren);

	SDL_Rect d = { 50, 50, 8, 8 };

	SDL_RenderCopy(g_ren, map, NULL, &d);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 50, 50) == 0xFFC86432u, "target contents copied");
	CHECK(px_at(g_ren, 57, 57) == 0xFFC86432u, "target contents BR");

	/* non-TARGET texture must be rejected */
	SDL_Texture *s = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
					   SDL_TEXTUREACCESS_STREAMING, 2, 2);

	CHECK(SDL_SetRenderTarget(g_ren, s) == -1, "non-target rejected");

	SDL_DestroyTexture(map);
	SDL_DestroyTexture(s);
}

static void test_texture_from_surface(void)
{
	/* ARGB surface with a translucent pixel -> BLEND texture */
	SDL_Surface *surf = SDL_CreateRGBSurface(0, 2, 1, 32,
						 0x00FF0000, 0x0000FF00,
						 0x000000FF, 0xFF000000);
	uint32_t *sp = surf->pixels;

	sp[0] = 0xFFFF0000u;
	sp[1] = 0x00000000u;

	SDL_Texture *t = SDL_CreateTextureFromSurface(g_ren, surf);

	CHECK(t != NULL, "texture from ARGB surface");

	SDL_SetRenderDrawColor(g_ren, 0, 0, 255, 255);
	SDL_RenderClear(g_ren);

	SDL_Rect d = { 0, 0, 2, 1 };

	SDL_RenderCopy(g_ren, t, NULL, &d);
	SDL_RenderPresent(g_ren);
	CHECK(px_at(g_ren, 0, 0) == 0xFFFF0000u, "opaque texel");
	CHECK(px_at(g_ren, 1, 0) == 0xFF0000FFu, "transparent texel skipped");

	SDL_DestroyTexture(t);
	SDL_FreeSurface(surf);
}

static void test_pimg(void)
{
	/* Synthetic INDEX8 2x2 PIMG: header + 2 palette entries + pixels */
	static uint8_t blob[12 + 8 + 4];
	uint8_t *p = blob;

	memcpy(p, "PIMG", 4);
	p[4] = 2; p[5] = 0;          /* w = 2 */
	p[6] = 2; p[7] = 0;          /* h = 2 */
	p[8] = 0;                    /* kind = INDEX8 */
	p[9] = 0;
	p[10] = 2; p[11] = 0;        /* ncolors = 2 */
	/* palette: red opaque, green a=128 */
	p[12] = 255; p[13] = 0; p[14] = 0; p[15] = 255;
	p[16] = 0; p[17] = 255; p[18] = 0; p[19] = 128;
	/* pixels */
	p[20] = 0; p[21] = 1; p[22] = 1; p[23] = 0;

	asset_add("test/idx.png", blob, sizeof(blob));

	SDL_Surface *surf = IMG_Load("test/idx.png");

	CHECK(surf != NULL, "IMG_Load INDEX8: %s", SDL_GetError());
	if (surf != NULL) {
		CHECK(surf->w == 2 && surf->h == 2, "dims");
		CHECK(surf->format->BitsPerPixel == 8, "indexed");
		CHECK(surf->format->palette->colors[0].r == 255, "pal[0].r");
		CHECK(surf->format->palette->colors[1].a == 128, "pal[1].a");
		CHECK(((uint8_t *)surf->pixels)[0] == 0, "px[0,0]");
		CHECK(((uint8_t *)surf->pixels)[1] == 1, "px[1,0]");

		/* palette alpha < 255 -> texture blends */
		SDL_Texture *t = SDL_CreateTextureFromSurface(g_ren, surf);
		Uint32 fmt;
		int acc, w, h;

		CHECK(t != NULL, "texture from indexed surface");
		SDL_QueryTexture(t, &fmt, &acc, &w, &h);
		CHECK(w == 2 && h == 2, "query dims");
		SDL_DestroyTexture(t);
		SDL_FreeSurface(surf);
	}

	/* ARGB32 1x1: bytes B,G,R,A */
	static uint8_t blob32[12 + 4];

	memcpy(blob32, "PIMG", 4);
	blob32[4] = 1; blob32[5] = 0;
	blob32[6] = 1; blob32[7] = 0;
	blob32[8] = 2;               /* kind = ARGB32 */
	blob32[9] = 0;
	blob32[10] = 0; blob32[11] = 0;
	blob32[12] = 0x11;           /* B */
	blob32[13] = 0x22;           /* G */
	blob32[14] = 0x33;           /* R */
	blob32[15] = 0x44;           /* A */

	asset_add("test/argb.png", blob32, sizeof(blob32));

	SDL_Surface *s32 = IMG_Load("test/argb.png");

	CHECK(s32 != NULL, "IMG_Load ARGB32: %s", SDL_GetError());
	if (s32 != NULL) {
		CHECK(*(uint32_t *)s32->pixels == 0x44332211u,
		      "ARGB word = %08x", *(uint32_t *)s32->pixels);
		SDL_FreeSurface(s32);
	}
}

static void test_ttf(void)
{
	/* Synthetic atlas: 'A' = full 3x5 block a=255 advance 4;
	 * 'B' = 2x2 block a=128 at xoff 1 / yoff 2, advance 3.
	 */
	enum { AW = 8, AH = 5 };
	static uint8_t blob[sizeof(struct s2sa_header) +
			    2 * sizeof(struct s2sa_glyph) + AW * AH];
	struct s2sa_header *hdr = (struct s2sa_header *)blob;

	memcpy(hdr->magic, S2SA_MAGIC, 4);
	hdr->first_cp = 'A';
	hdr->ncp = 2;
	hdr->ascent = 5;
	hdr->descent = 1;
	hdr->atlas_w = AW;
	hdr->atlas_h = AH;
	hdr->px_size = 4;

	struct s2sa_glyph *g = (struct s2sa_glyph *)(blob + sizeof(*hdr));

	g[0] = (struct s2sa_glyph){ .x = 0, .y = 0, .w = 3, .h = 5,
				    .xoff = 0, .yoff = 0, .advance = 4 };
	g[1] = (struct s2sa_glyph){ .x = 3, .y = 0, .w = 2, .h = 2,
				    .xoff = 1, .yoff = 2, .advance = 3 };

	uint8_t *cov = blob + sizeof(*hdr) + 2 * sizeof(*g);

	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 3; x++) {
			cov[y * AW + x] = 255;
		}
	}
	for (int y = 0; y < 2; y++) {
		for (int x = 3; x < 5; x++) {
			cov[y * AW + x] = 128;
		}
	}

	asset_add("Font/TEST.TTF@4", blob, sizeof(blob));

	TTF_Font *font = TTF_OpenFont("Font/TEST.TTF", 4);

	CHECK(font != NULL, "TTF_OpenFont: %s", SDL_GetError());
	if (font == NULL) {
		return;
	}
	CHECK(TTF_FontHeight(font) == 6, "font height");

	int w, h;

	TTF_SizeText(font, "AB", &w, &h);
	CHECK(w == 7 && h == 6, "SizeText AB = %dx%d", w, h);

	SDL_Color white = { 255, 255, 255, 255 };
	SDL_Surface *surf = TTF_RenderText_Blended(font, "AB", white);

	CHECK(surf != NULL, "RenderText: %s", SDL_GetError());
	if (surf != NULL) {
		CHECK(surf->w == 7 && surf->h == 6, "surface dims %dx%d",
		      surf->w, surf->h);

		uint32_t *row0 = surf->pixels;

		CHECK(row0[0] == 0xFFFFFFFFu, "A texel (0,0) = %08x", row0[0]);

		uint32_t *row2 = (uint32_t *)((uint8_t *)surf->pixels +
					      2 * surf->pitch);

		CHECK(row2[5] == 0x80FFFFFFu, "B texel (5,2) = %08x", row2[5]);
		CHECK(row2[3] == 0x00000000u, "gap texel (3,2)");

		uint32_t *row5 = (uint32_t *)((uint8_t *)surf->pixels +
					      5 * surf->pitch);

		CHECK(row5[6] == 0x00000000u, "below-B texel empty");
		SDL_FreeSurface(surf);
	}

	/* missing glyph: renders as blank advance, doesn't crash */
	SDL_Surface *m = TTF_RenderText_Blended(font, "Az", white);

	CHECK(m != NULL, "missing glyph tolerated");
	if (m != NULL) {
		CHECK(m->w == 4 + hdr->px_size, "missing glyph advance");
		SDL_FreeSurface(m);
	}

	TTF_CloseFont(font);
}

int main(void)
{
	SDL_Window *win = SDL_CreateWindow("t", 0, 0, 640, 480, 0);

	g_ren = SDL_CreateRenderer(win, -1, 0);
	if (g_ren == NULL) {
		fprintf(stderr, "renderer create failed: %s\n", SDL_GetError());
		return 1;
	}

	test_clear_and_fill();
	test_lines_and_rect_outline();
	test_texture_copy_and_blend();
	test_scaled_copy();
	test_render_target();
	test_texture_from_surface();
	test_pimg();
	test_ttf();

	if (failures != 0) {
		dump_ppm("out/last_frame.ppm");
		fprintf(stderr, "%d FAILURES (backing dumped to out/last_frame.ppm)\n",
			failures);
		return 1;
	}
	printf("render_host: all tests passed (%d frames presented)\n",
	       present_frames);
	return 0;
}
