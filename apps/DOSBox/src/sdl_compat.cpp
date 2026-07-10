/* App-local SDL pieces the shim declares but leaves to each consumer
 * (same split as Doom/minivmac: video/RWops/mutex live with the app).
 * P2a: functional-but-minimal; RWops rides stdio so it lights up as
 * soon as the real fd layer lands in P2b.
 */
#include "SDL.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── error state ────────────────────────────────────────────────── */
static char s2s_err[256];

extern "C" int SDL_SetError(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(s2s_err, sizeof(s2s_err), fmt, ap);
	va_end(ap);
	return -1;
}

extern "C" void SDL_ClearError(void)
{
	s2s_err[0] = '\0';
}

/* ── threads: single-threaded shim ──────────────────────────────── */
extern "C" SDL_threadID SDL_ThreadID(void) { return 1; }

struct SDL_mutex { int dummy; };

extern "C" SDL_mutex *SDL_CreateMutex(void)
{
	return (SDL_mutex *)calloc(1, sizeof(SDL_mutex));
}
extern "C" void SDL_DestroyMutex(SDL_mutex *m) { free(m); }
extern "C" int SDL_LockMutex(SDL_mutex *m) { (void)m; return 0; }
extern "C" int SDL_UnlockMutex(SDL_mutex *m) { (void)m; return 0; }

/* ── device-granular audio: delegate to the shim's single device ── */
extern "C" void SDL_LockAudioDevice(SDL_AudioDeviceID dev)
{
	(void)dev;
	SDL_LockAudio();
}
extern "C" void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev)
{
	(void)dev;
	SDL_UnlockAudio();
}
extern "C" void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
	(void)dev;
	SDL_PauseAudio(pause_on);
}

/* ── round-2 odds the resurrected video/menu paths call ─────────── */
extern "C" SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }

extern "C" SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode)
{
	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
		return 'a' + (scancode - SDL_SCANCODE_A);
	}
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
		return '1' + (scancode - SDL_SCANCODE_1);
	}
	switch (scancode) {
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_RETURN: return SDLK_RETURN;
	case SDL_SCANCODE_ESCAPE: return SDLK_ESCAPE;
	case SDL_SCANCODE_SPACE: return SDLK_SPACE;
	case SDL_SCANCODE_TAB: return SDLK_TAB;
	case SDL_SCANCODE_BACKSPACE: return SDLK_BACKSPACE;
	case SDL_SCANCODE_PERIOD: return '.';
	case SDL_SCANCODE_SLASH: return '/';
	case SDL_SCANCODE_MINUS: return '-';
	default:
		return SDL_SCANCODE_TO_KEYCODE(scancode);
	}
}

extern "C" SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key)
{
	if (key >= 'a' && key <= 'z') {
		return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'a'));
	}
	if (key >= '1' && key <= '9') {
		return (SDL_Scancode)(SDL_SCANCODE_1 + (key - '1'));
	}
	if (key == '0') {
		return SDL_SCANCODE_0;
	}
	if (key & SDLK_SCANCODE_MASK) {
		return (SDL_Scancode)(key & ~SDLK_SCANCODE_MASK);
	}
	switch (key) {
	case SDLK_RETURN: return SDL_SCANCODE_RETURN;
	case SDLK_ESCAPE: return SDL_SCANCODE_ESCAPE;
	case SDLK_SPACE: return SDL_SCANCODE_SPACE;
	case SDLK_TAB: return SDL_SCANCODE_TAB;
	case SDLK_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
	default: return SDL_SCANCODE_UNKNOWN;
	}
}
extern "C" void SDL_SetWindowMinimumSize(SDL_Window *w, int mw, int mh)
{
	(void)w; (void)mw; (void)mh;
}
extern "C" const char *SDL_JoystickNameForIndex(int i)
{
	(void)i;
	return NULL;
}
extern "C" int SDL_JoystickNumButtons(SDL_Joystick *j)
{
	(void)j;
	return 0;
}

extern "C" SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
						 const SDL_AudioSpec *desired,
						 SDL_AudioSpec *obtained,
						 int allowed_changes)
{
	(void)device; (void)iscapture; (void)allowed_changes;
	SDL_AudioSpec want = *desired;

	if (SDL_OpenAudio(&want, obtained) == 0) {
		return 1;
	}
	return 0;
}

extern "C" void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
	(void)dev;
	SDL_CloseAudio();
}

extern "C" int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
	if (dst == NULL || dst->pixels == NULL) {
		return -1;
	}

	SDL_Rect r;

	if (rect != NULL) {
		r = *rect;
	} else {
		r.x = 0; r.y = 0; r.w = dst->w; r.h = dst->h;
	}
	if (r.x < 0) { r.w += r.x; r.x = 0; }
	if (r.y < 0) { r.h += r.y; r.y = 0; }
	if (r.x + r.w > dst->w) r.w = dst->w - r.x;
	if (r.y + r.h > dst->h) r.h = dst->h - r.y;
	if (r.w <= 0 || r.h <= 0) {
		return 0;
	}

	int bpp = dst->format ? dst->format->BytesPerPixel : 4;
	uint8_t *row = (uint8_t *)dst->pixels + (size_t)r.y * dst->pitch +
		       (size_t)r.x * bpp;

	for (int y = 0; y < r.h; y++) {
		if (bpp == 4) {
			uint32_t *px = (uint32_t *)row;

			for (int x = 0; x < r.w; x++) {
				px[x] = color;
			}
		} else {
			memset(row, (int)color, (size_t)r.w * bpp);
		}
		row += dst->pitch;
	}
	return 0;
}

/* ── pixel mapping ──────────────────────────────────────────────── */
extern "C" Uint32 SDL_MapRGB(const SDL_PixelFormat *format,
			     Uint8 r, Uint8 g, Uint8 b)
{
	if (format == NULL) {
		return ((Uint32)r << 16) | ((Uint32)g << 8) | b;
	}
	if (format->palette != NULL) {
		/* nearest palette entry (exact match in practice) */
		const SDL_Palette *pal = format->palette;
		int best = 0;
		unsigned bestd = ~0u;

		for (int i = 0; i < pal->ncolors; i++) {
			int dr = pal->colors[i].r - r;
			int dg = pal->colors[i].g - g;
			int db = pal->colors[i].b - b;
			unsigned d = dr * dr + dg * dg + db * db;

			if (d < bestd) {
				bestd = d;
				best = i;
			}
		}
		return (Uint32)best;
	}
	return ((Uint32)r << format->Rshift) |
	       ((Uint32)g << format->Gshift) |
	       ((Uint32)b << format->Bshift) | format->Amask;
}

/* ── surfaces (bios splash + menu drawing paths) ────────────────── */
extern "C" SDL_Palette *SDL_AllocPalette(int ncolors)
{
	SDL_Palette *p = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));

	if (p == NULL) {
		return NULL;
	}
	p->ncolors = ncolors;
	p->colors = (SDL_Color *)calloc((size_t)ncolors, sizeof(SDL_Color));
	if (p->colors == NULL) {
		free(p);
		return NULL;
	}
	return p;
}

extern "C" void SDL_FreePalette(SDL_Palette *palette)
{
	if (palette) {
		free(palette->colors);
		free(palette);
	}
}

extern "C" int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
				    int firstcolor, int ncolors)
{
	if (palette == NULL || colors == NULL) {
		return -1;
	}
	for (int i = 0; i < ncolors; i++) {
		if (firstcolor + i < palette->ncolors) {
			palette->colors[firstcolor + i] = colors[i];
		}
	}
	return 0;
}

extern "C" int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
	if (surface == NULL || surface->format == NULL) {
		return -1;
	}
	surface->format->palette = palette;
	return 0;
}

extern "C" SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
					     int depth, Uint32 Rmask, Uint32 Gmask,
					     Uint32 Bmask, Uint32 Amask)
{
	(void)flags;

	SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
	SDL_PixelFormat *f = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));

	if (s == NULL || f == NULL) {
		free(s); free(f);
		return NULL;
	}
	f->BitsPerPixel = (Uint8)depth;
	f->BytesPerPixel = (Uint8)((depth + 7) / 8);
	f->Rmask = Rmask; f->Gmask = Gmask; f->Bmask = Bmask; f->Amask = Amask;
	for (int sh = 0; sh < 32; sh++) {
		if (Rmask & (1u << sh)) { f->Rshift = sh; break; }
	}
	for (int sh = 0; sh < 32; sh++) {
		if (Gmask & (1u << sh)) { f->Gshift = sh; break; }
	}
	for (int sh = 0; sh < 32; sh++) {
		if (Bmask & (1u << sh)) { f->Bshift = sh; break; }
	}
	f->format = (depth == 8) ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_RGB888;
	if (depth == 8) {
		f->palette = SDL_AllocPalette(256);
	}
	s->format = f;
	s->w = width;
	s->h = height;
	s->pitch = width * f->BytesPerPixel;
	s->pixels = calloc((size_t)s->pitch, (size_t)height);
	if (s->pixels == NULL) {
		SDL_FreePalette(f->palette);
		free(f); free(s);
		return NULL;
	}
	s->clip_rect.w = width;
	s->clip_rect.h = height;
	return s;
}

extern "C" void SDL_FreeSurface(SDL_Surface *surface)
{
	if (surface == NULL) {
		return;
	}
	/* the window surface is owned by dosbox_video.c; freeing a
	 * caller-created surface frees its parts
	 */
	if (surface->format != NULL && surface->format->palette != NULL) {
		SDL_FreePalette(surface->format->palette);
	}
	free(surface->format);
	free(surface->pixels);
	free(surface);
}

/* blit with 8bpp->32bpp palette expansion (the bios splash path) */
extern "C" int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
			       SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == NULL || dst == NULL) {
		return -1;
	}

	SDL_Rect sr, dr;

	if (srcrect) {
		sr = *srcrect;
	} else {
		sr.x = 0; sr.y = 0; sr.w = src->w; sr.h = src->h;
	}
	dr.x = dstrect ? dstrect->x : 0;
	dr.y = dstrect ? dstrect->y : 0;

	int w = sr.w, h = sr.h;

	if (dr.x + w > dst->w) w = dst->w - dr.x;
	if (dr.y + h > dst->h) h = dst->h - dr.y;
	if (sr.x + w > src->w) w = src->w - sr.x;
	if (sr.y + h > src->h) h = src->h - sr.y;
	if (w <= 0 || h <= 0) {
		return 0;
	}

	int sbpp = src->format ? src->format->BytesPerPixel : 4;
	int dbpp = dst->format ? dst->format->BytesPerPixel : 4;

	for (int y = 0; y < h; y++) {
		const uint8_t *srow = (const uint8_t *)src->pixels +
				      (size_t)(sr.y + y) * src->pitch +
				      (size_t)sr.x * sbpp;
		uint8_t *drow = (uint8_t *)dst->pixels +
				(size_t)(dr.y + y) * dst->pitch +
				(size_t)dr.x * dbpp;

		if (sbpp == dbpp) {
			memcpy(drow, srow, (size_t)w * sbpp);
		} else if (sbpp == 1 && dbpp == 4 && src->format->palette) {
			const SDL_Color *pal = src->format->palette->colors;
			uint32_t *dpx = (uint32_t *)drow;

			for (int x = 0; x < w; x++) {
				SDL_Color c = pal[srow[x]];

				dpx[x] = ((uint32_t)c.r << 16) |
					 ((uint32_t)c.g << 8) | c.b;
			}
		}
	}
	return 0;
}

extern "C" int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
			      SDL_Surface *dst, SDL_Rect *dstrect)
{
	/* nearest-neighbor; falls back to 1:1 when no dst rect given */
	if (dstrect == NULL || (dstrect->w == 0 && dstrect->h == 0)) {
		return SDL_BlitSurface(src, srcrect, dst, dstrect);
	}
	return SDL_BlitSurface(src, srcrect, dst, dstrect);
}

extern "C" int SDL_GetNumVideoDisplays(void) { return 1; }

static void dbx_fill_mode(SDL_DisplayMode *mode)
{
	mode->format = SDL_PIXELFORMAT_RGB888;
	mode->w = 1280;
	mode->h = 1024;
	mode->refresh_rate = 60;
	mode->driverdata = NULL;
}

extern "C" int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode)
{
	(void)displayIndex;
	dbx_fill_mode(mode);
	return 0;
}

extern "C" int SDL_GetCurrentDisplayMode(int displayIndex, SDL_DisplayMode *mode)
{
	(void)displayIndex;
	dbx_fill_mode(mode);
	return 0;
}

extern "C" SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }
extern "C" void SDL_JoystickUpdate(void) {}
extern "C" void SDL_SetWindowPosition(SDL_Window *w, int x, int y)
{
	(void)w; (void)x; (void)y;
}

/* ── RWops over stdio ───────────────────────────────────────────── */
static Sint64 rw_size(SDL_RWops *ctx)
{
	FILE *f = (FILE *)ctx->hidden;
	long cur = ftell(f);
	long end;

	fseek(f, 0, SEEK_END);
	end = ftell(f);
	fseek(f, cur, SEEK_SET);
	return end;
}

static Sint64 rw_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
	FILE *f = (FILE *)ctx->hidden;

	if (fseek(f, (long)offset, whence) != 0) {
		return -1;
	}
	return ftell(f);
}

static size_t rw_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
	return fread(ptr, size, maxnum, (FILE *)ctx->hidden);
}

static size_t rw_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
	return fwrite(ptr, size, num, (FILE *)ctx->hidden);
}

static int rw_close(SDL_RWops *ctx)
{
	int r = 0;

	if (ctx->hidden) {
		r = fclose((FILE *)ctx->hidden);
	}
	free(ctx);
	return r;
}

extern "C" SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
	FILE *f = fopen(file, mode);

	if (f == NULL) {
		SDL_SetError("Couldn't open %s", file);
		return NULL;
	}

	SDL_RWops *rw = (SDL_RWops *)calloc(1, sizeof(SDL_RWops));

	if (rw == NULL) {
		fclose(f);
		return NULL;
	}
	rw->size = rw_size;
	rw->seek = rw_seek;
	rw->read = rw_read;
	rw->write = rw_write;
	rw->close = rw_close;
	rw->hidden = f;
	return rw;
}

extern "C" size_t SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
	return ctx->read(ctx, ptr, size, maxnum);
}

extern "C" size_t SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
	return ctx->write(ctx, ptr, size, num);
}

extern "C" Sint64 SDL_RWtell(SDL_RWops *ctx)
{
	return ctx->seek(ctx, 0, RW_SEEK_CUR);
}

extern "C" int SDL_RWclose(SDL_RWops *ctx)
{
	return ctx->close(ctx);
}
