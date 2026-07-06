/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- init/error/version/hints, the single "window",
 * and misc surface-independent calls (messagebox, iconv, names).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdarg.h>
#include <stdio.h>

#include "sdl2shim.h"

LOG_MODULE_REGISTER(s2s_core, CONFIG_SDL2SHIM_LOG_LEVEL);

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

int SDL_Init(Uint32 flags)
{
	ARG_UNUSED(flags);
	return 0;
}

int SDL_InitSubSystem(Uint32 flags)
{
	ARG_UNUSED(flags);
	return 0;
}

void SDL_Quit(void)
{
}

void SDL_QuitSubSystem(Uint32 flags)
{
	ARG_UNUSED(flags);
}

SDL_bool SDL_SetHint(const char *name, const char *value)
{
	LOG_DBG("hint %s=%s", name, value);
	return SDL_TRUE;
}

void SDL_GetVersion(SDL_version *ver)
{
	SDL_VERSION(ver);
}

/* ── window ──────────────────────────────────────────────────── */

struct SDL_Window {
	int w, h;
	Uint32 flags;
};

static struct SDL_Window the_window;

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	ARG_UNUSED(title);
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	the_window.w = w;
	the_window.h = h;
	the_window.flags = flags;
	LOG_INF("window %dx%d flags 0x%08x", w, h, flags);
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

Uint32 SDL_GetWindowFlags(SDL_Window *window)
{
	return window->flags;
}

int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
	window->flags = (window->flags & ~SDL_WINDOW_FULLSCREEN_DESKTOP) | flags;
	return 0;
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
	ARG_UNUSED(window);
	ARG_UNUSED(title);
}

void SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon)
{
	ARG_UNUSED(window);
	ARG_UNUSED(icon);
}

void SDL_DestroyWindow(SDL_Window *window)
{
	ARG_UNUSED(window);
	/* The single window is static storage; nothing to free. */
}

void SDL_GL_GetDrawableSize(SDL_Window *window, int *w, int *h)
{
	SDL_GetWindowSize(window, w, h);
}

/* ── misc ────────────────────────────────────────────────────── */

int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message,
			     SDL_Window *window)
{
	ARG_UNUSED(flags);
	ARG_UNUSED(window);
	LOG_ERR("[%s] %s", title, message);
	return 0;
}

char *SDL_iconv_string(const char *tocode, const char *fromcode,
		       const char *inbuf, size_t inbytesleft)
{
	ARG_UNUSED(tocode);
	ARG_UNUSED(fromcode);

	/* UTF-8 passthrough suffices per the manifest (bucket A). */
	char *out = malloc(inbytesleft + 1);

	if (out == NULL) {
		return NULL;
	}
	memcpy(out, inbuf, inbytesleft);
	out[inbytesleft] = '\0';
	return out;
}

const char *SDL_GetPixelFormatName(Uint32 format)
{
	switch (format) {
	case SDL_PIXELFORMAT_INDEX8:
		return "SDL_PIXELFORMAT_INDEX8";
	case SDL_PIXELFORMAT_RGB24:
		return "SDL_PIXELFORMAT_RGB24";
	case SDL_PIXELFORMAT_ARGB8888:
		return "SDL_PIXELFORMAT_ARGB8888";
	default:
		return "SDL_PIXELFORMAT_UNKNOWN";
	}
}

const char *SDL_GetScancodeName(SDL_Scancode scancode)
{
	static const char *const letters[] = {
		"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
		"N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
	};
	static const char *const digits[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
	};

	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
		return letters[scancode - SDL_SCANCODE_A];
	}
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
		return digits[scancode - SDL_SCANCODE_1];
	}
	switch (scancode) {
	case SDL_SCANCODE_RETURN:    return "Return";
	case SDL_SCANCODE_ESCAPE:    return "Escape";
	case SDL_SCANCODE_BACKSPACE: return "Backspace";
	case SDL_SCANCODE_TAB:       return "Tab";
	case SDL_SCANCODE_SPACE:     return "Space";
	case SDL_SCANCODE_LEFT:      return "Left";
	case SDL_SCANCODE_RIGHT:     return "Right";
	case SDL_SCANCODE_UP:        return "Up";
	case SDL_SCANCODE_DOWN:      return "Down";
	case SDL_SCANCODE_HOME:      return "Home";
	case SDL_SCANCODE_END:       return "End";
	case SDL_SCANCODE_PAGEUP:    return "PageUp";
	case SDL_SCANCODE_PAGEDOWN:  return "PageDown";
	case SDL_SCANCODE_DELETE:    return "Delete";
	case SDL_SCANCODE_LSHIFT:    return "Left Shift";
	case SDL_SCANCODE_RSHIFT:    return "Right Shift";
	case SDL_SCANCODE_LCTRL:     return "Left Ctrl";
	case SDL_SCANCODE_RCTRL:     return "Right Ctrl";
	case SDL_SCANCODE_LALT:      return "Left Alt";
	case SDL_SCANCODE_RALT:      return "Right Alt";
	default:                     return "";
	}
}
