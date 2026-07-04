/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- minimal SDL2 API surface for SDLPoP on Zephyr.
 *
 * This is NOT a general SDL2 port. It declares exactly the symbols
 * SDLPoP references (work-order §2 manifest, grep-verified) and no
 * more. Struct layouts match what the game pokes at directly
 * (surface->pixels, format->palette->colors, event.key.keysym, ...);
 * enum values follow upstream SDL2 where an external contract exists
 * (USB-HID scancode values, event type numbers), and are otherwise
 * internal to this shim.
 *
 * Implementations live in os/sdlpop/src/shim_*.c, backed by Zephyr
 * (k_uptime/k_timer, the bcm2835_fb present path, fs_*).
 */

#ifndef PIZZA_SDL2_SDL_H
#define PIZZA_SDL2_SDL_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── stdinc: basic types ─────────────────────────────────────── */

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#define SDL_COMPILE_TIME_ASSERT(name, x) \
	typedef int SDL_dummy_##name[(x) * 2 - 1]

#define SDL_memset  memset
#define SDL_strlen  strlen
#define SDL_wcslen  wcslen
#define SDL_free    free
#define SDL_malloc  malloc

char *SDL_iconv_string(const char *tocode, const char *fromcode,
		       const char *inbuf, size_t inbytesleft);

/* ── endian ──────────────────────────────────────────────────── */

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_LIL_ENDIAN   /* AArch64 LE */

static inline Uint16 SDL_Swap16(Uint16 x) { return (Uint16)__builtin_bswap16(x); }
static inline Uint32 SDL_Swap32(Uint32 x) { return __builtin_bswap32(x); }

#define SDL_SwapLE16(x) ((Uint16)(x))
#define SDL_SwapLE32(x) ((Uint32)(x))
#define SDL_SwapBE16(x) SDL_Swap16(x)
#define SDL_SwapBE32(x) SDL_Swap32(x)

/* ── version ─────────────────────────────────────────────────── */

typedef struct SDL_version {
	Uint8 major;
	Uint8 minor;
	Uint8 patch;
} SDL_version;

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL    22

#define SDL_VERSION(v)                     \
	do {                               \
		(v)->major = SDL_MAJOR_VERSION; \
		(v)->minor = SDL_MINOR_VERSION; \
		(v)->patch = SDL_PATCHLEVEL;    \
	} while (0)

#define SDL_VERSIONNUM(X, Y, Z) ((X) * 1000 + (Y) * 100 + (Z))
#define SDL_VERSION_ATLEAST(X, Y, Z) \
	(SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL) >= \
	 SDL_VERSIONNUM(X, Y, Z))

void SDL_GetVersion(SDL_version *ver);

/* ── init / error / hints ────────────────────────────────────── */

#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_NOPARACHUTE    0x00100000u

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);

#define SDL_HINT_RENDER_SCALE_QUALITY        "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_RENDER_VSYNC                "SDL_RENDER_VSYNC"
#define SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING "SDL_WINDOWS_DISABLE_THREAD_NAMING"
#define SDL_HINT_IME_SHOW_UI                 "SDL_IME_SHOW_UI"

SDL_bool SDL_SetHint(const char *name, const char *value);

/* ── rect / pixels / palette / surface ───────────────────────── */

typedef struct SDL_Rect {
	int x, y;
	int w, h;
} SDL_Rect;

typedef struct SDL_Color {
	Uint8 r, g, b, a;
} SDL_Color;

#define SDL_ALPHA_OPAQUE      255
#define SDL_ALPHA_TRANSPARENT 0

typedef struct SDL_Palette {
	int ncolors;
	SDL_Color *colors;
} SDL_Palette;

/* Internal format ids -- NOT upstream SDL2 FOURCC values. Nothing
 * serializes these; SDL_ISPIXELFORMAT_INDEXED is the only structural
 * consumer in the game.
 */
enum {
	SDL_PIXELFORMAT_UNKNOWN = 0,
	SDL_PIXELFORMAT_INDEX8,
	SDL_PIXELFORMAT_RGB24,
	SDL_PIXELFORMAT_ARGB8888,
};

#define SDL_ISPIXELFORMAT_INDEXED(f) ((f) == SDL_PIXELFORMAT_INDEX8)

typedef struct SDL_PixelFormat {
	Uint32 format;
	SDL_Palette *palette;
	Uint8 BitsPerPixel;
	Uint8 BytesPerPixel;
	Uint32 Rmask, Gmask, Bmask, Amask;
	Uint8 Rshift, Gshift, Bshift, Ashift;
} SDL_PixelFormat;

typedef struct SDL_Surface {
	Uint32 flags;
	SDL_PixelFormat *format;
	int w, h;
	int pitch;
	void *pixels;
	SDL_Rect clip_rect;
	int refcount;
} SDL_Surface;

typedef enum {
	SDL_BLENDMODE_NONE  = 0x0,
	SDL_BLENDMODE_BLEND = 0x1,
	SDL_BLENDMODE_ADD   = 0x2,
	SDL_BLENDMODE_MOD   = 0x4,
} SDL_BlendMode;

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
				  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
		    SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
		   SDL_Surface *dst, SDL_Rect *dstrect);
SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags);
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags);
SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha);
int SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode blendMode);
int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
			 int firstcolor, int ncolors);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
const char *SDL_GetPixelFormatName(Uint32 format);

/* ── window / renderer / texture ─────────────────────────────── */

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000

#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_RESIZABLE          0x00000020u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)
#define SDL_WINDOW_ALLOW_HIGHDPI      0x00002000u

#define SDL_RENDERER_SOFTWARE      0x00000001u
#define SDL_RENDERER_ACCELERATED   0x00000002u
#define SDL_RENDERER_TARGETTEXTURE 0x00000008u

#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET    2

typedef struct SDL_RendererInfo {
	const char *name;
	Uint32 flags;
	Uint32 num_texture_formats;
	Uint32 texture_formats[16];
	int max_texture_width;
	int max_texture_height;
} SDL_RendererInfo;

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
Uint32 SDL_GetWindowFlags(SDL_Window *window);
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);
void SDL_SetWindowTitle(SDL_Window *window, const char *title);
void SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon);

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
		      const void *pixels, int pitch);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
		   const SDL_Rect *srcrect, const SDL_Rect *dstrect);
void SDL_RenderPresent(SDL_Renderer *renderer);
int SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h);
void SDL_RenderGetLogicalSize(SDL_Renderer *renderer, int *w, int *h);
int SDL_RenderSetIntegerScale(SDL_Renderer *renderer, SDL_bool enable);
void SDL_RenderGetScale(SDL_Renderer *renderer, float *scaleX, float *scaleY);
void SDL_RenderGetViewport(SDL_Renderer *renderer, SDL_Rect *rect);
int SDL_GetRendererInfo(SDL_Renderer *renderer, SDL_RendererInfo *info);
int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h);

/* ── scancodes / keycodes / mods ─────────────────────────────── */

/* Values are the upstream SDL2 / USB-HID usage numbers -- an external
 * contract (replay files, future HOGP keyboard reports map 1:1).
 */
typedef enum {
	SDL_SCANCODE_UNKNOWN = 0,
	SDL_SCANCODE_A = 4, SDL_SCANCODE_B = 5, SDL_SCANCODE_C = 6,
	SDL_SCANCODE_D = 7, SDL_SCANCODE_E = 8, SDL_SCANCODE_F = 9,
	SDL_SCANCODE_G = 10, SDL_SCANCODE_H = 11, SDL_SCANCODE_I = 12,
	SDL_SCANCODE_J = 13, SDL_SCANCODE_K = 14, SDL_SCANCODE_L = 15,
	SDL_SCANCODE_M = 16, SDL_SCANCODE_N = 17, SDL_SCANCODE_O = 18,
	SDL_SCANCODE_P = 19, SDL_SCANCODE_Q = 20, SDL_SCANCODE_R = 21,
	SDL_SCANCODE_S = 22, SDL_SCANCODE_T = 23, SDL_SCANCODE_U = 24,
	SDL_SCANCODE_V = 25, SDL_SCANCODE_W = 26, SDL_SCANCODE_X = 27,
	SDL_SCANCODE_Y = 28, SDL_SCANCODE_Z = 29,
	SDL_SCANCODE_1 = 30, SDL_SCANCODE_2 = 31, SDL_SCANCODE_3 = 32,
	SDL_SCANCODE_4 = 33, SDL_SCANCODE_5 = 34, SDL_SCANCODE_6 = 35,
	SDL_SCANCODE_7 = 36, SDL_SCANCODE_8 = 37, SDL_SCANCODE_9 = 38,
	SDL_SCANCODE_0 = 39,
	SDL_SCANCODE_RETURN = 40,
	SDL_SCANCODE_ESCAPE = 41,
	SDL_SCANCODE_BACKSPACE = 42,
	SDL_SCANCODE_TAB = 43,
	SDL_SCANCODE_SPACE = 44,
	SDL_SCANCODE_MINUS = 45,
	SDL_SCANCODE_EQUALS = 46,
	SDL_SCANCODE_LEFTBRACKET = 47,
	SDL_SCANCODE_RIGHTBRACKET = 48,
	SDL_SCANCODE_BACKSLASH = 49,
	SDL_SCANCODE_SEMICOLON = 51,
	SDL_SCANCODE_APOSTROPHE = 52,
	SDL_SCANCODE_GRAVE = 53,
	SDL_SCANCODE_COMMA = 54,
	SDL_SCANCODE_PERIOD = 55,
	SDL_SCANCODE_SLASH = 56,
	SDL_SCANCODE_CAPSLOCK = 57,
	SDL_SCANCODE_F1 = 58, SDL_SCANCODE_F2 = 59, SDL_SCANCODE_F3 = 60,
	SDL_SCANCODE_F4 = 61, SDL_SCANCODE_F5 = 62, SDL_SCANCODE_F6 = 63,
	SDL_SCANCODE_F7 = 64, SDL_SCANCODE_F8 = 65, SDL_SCANCODE_F9 = 66,
	SDL_SCANCODE_F10 = 67, SDL_SCANCODE_F11 = 68, SDL_SCANCODE_F12 = 69,
	SDL_SCANCODE_PRINTSCREEN = 70,
	SDL_SCANCODE_SCROLLLOCK = 71,
	SDL_SCANCODE_PAUSE = 72,
	SDL_SCANCODE_INSERT = 73,
	SDL_SCANCODE_HOME = 74,
	SDL_SCANCODE_PAGEUP = 75,
	SDL_SCANCODE_DELETE = 76,
	SDL_SCANCODE_END = 77,
	SDL_SCANCODE_PAGEDOWN = 78,
	SDL_SCANCODE_RIGHT = 79,
	SDL_SCANCODE_LEFT = 80,
	SDL_SCANCODE_DOWN = 81,
	SDL_SCANCODE_UP = 82,
	SDL_SCANCODE_NUMLOCKCLEAR = 83,
	SDL_SCANCODE_KP_DIVIDE = 84,
	SDL_SCANCODE_KP_MULTIPLY = 85,
	SDL_SCANCODE_KP_MINUS = 86,
	SDL_SCANCODE_KP_PLUS = 87,
	SDL_SCANCODE_KP_ENTER = 88,
	SDL_SCANCODE_KP_1 = 89, SDL_SCANCODE_KP_2 = 90, SDL_SCANCODE_KP_3 = 91,
	SDL_SCANCODE_KP_4 = 92, SDL_SCANCODE_KP_5 = 93, SDL_SCANCODE_KP_6 = 94,
	SDL_SCANCODE_KP_7 = 95, SDL_SCANCODE_KP_8 = 96, SDL_SCANCODE_KP_9 = 97,
	SDL_SCANCODE_KP_0 = 98,
	SDL_SCANCODE_KP_PERIOD = 99,
	SDL_SCANCODE_APPLICATION = 101,
	SDL_SCANCODE_MUTE = 127,
	SDL_SCANCODE_VOLUMEUP = 128,
	SDL_SCANCODE_VOLUMEDOWN = 129,
	SDL_SCANCODE_CLEAR = 156,
	SDL_SCANCODE_LCTRL = 224,
	SDL_SCANCODE_LSHIFT = 225,
	SDL_SCANCODE_LALT = 226,
	SDL_SCANCODE_LGUI = 227,
	SDL_SCANCODE_RCTRL = 228,
	SDL_SCANCODE_RSHIFT = 229,
	SDL_SCANCODE_RALT = 230,
	SDL_SCANCODE_RGUI = 231,
	SDL_SCANCODE_AUDIOMUTE = 262,
	SDL_NUM_SCANCODES = 512,
} SDL_Scancode;

typedef Sint32 SDL_Keycode;

typedef enum {
	KMOD_NONE   = 0x0000,
	KMOD_LSHIFT = 0x0001,
	KMOD_RSHIFT = 0x0002,
	KMOD_LCTRL  = 0x0040,
	KMOD_RCTRL  = 0x0080,
	KMOD_LALT   = 0x0100,
	KMOD_RALT   = 0x0200,
	KMOD_LGUI   = 0x0400,
	KMOD_RGUI   = 0x0800,
} SDL_Keymod;

#define KMOD_SHIFT (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_CTRL  (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_ALT   (KMOD_LALT | KMOD_RALT)

typedef struct SDL_Keysym {
	SDL_Scancode scancode;
	SDL_Keycode sym;
	Uint16 mod;
	Uint32 unused;
} SDL_Keysym;

const Uint8 *SDL_GetKeyboardState(int *numkeys);
const char *SDL_GetScancodeName(SDL_Scancode scancode);

/* ── events ──────────────────────────────────────────────────── */

enum {
	SDL_QUIT = 0x100,
	SDL_WINDOWEVENT = 0x200,
	SDL_KEYDOWN = 0x300,
	SDL_KEYUP = 0x301,
	SDL_TEXTINPUT = 0x303,
	SDL_MOUSEMOTION = 0x400,
	SDL_MOUSEBUTTONDOWN = 0x401,
	SDL_MOUSEBUTTONUP = 0x402,
	SDL_MOUSEWHEEL = 0x403,
	SDL_JOYAXISMOTION = 0x600,
	SDL_JOYBUTTONDOWN = 0x603,
	SDL_JOYBUTTONUP = 0x604,
	SDL_CONTROLLERAXISMOTION = 0x650,
	SDL_CONTROLLERBUTTONDOWN = 0x651,
	SDL_CONTROLLERBUTTONUP = 0x652,
	SDL_CONTROLLERDEVICEADDED = 0x653,
	SDL_CONTROLLERDEVICEREMOVED = 0x654,
	SDL_USEREVENT = 0x8000,
	SDL_LASTEVENT = 0xFFFF,
};

enum {
	SDL_WINDOWEVENT_EXPOSED = 3,
	SDL_WINDOWEVENT_MOVED = 4,
	SDL_WINDOWEVENT_SIZE_CHANGED = 6,
	SDL_WINDOWEVENT_MINIMIZED = 7,
	SDL_WINDOWEVENT_RESTORED = 9,
	SDL_WINDOWEVENT_FOCUS_GAINED = 12,
};

#define SDL_BUTTON_LEFT  1
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_X1    4

#define SDL_PRESSED  1
#define SDL_RELEASED 0

#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32

typedef struct SDL_CommonEvent {
	Uint32 type;
	Uint32 timestamp;
} SDL_CommonEvent;

typedef struct SDL_WindowEvent {
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint8 event;
	Sint32 data1, data2;
} SDL_WindowEvent;

typedef struct SDL_KeyboardEvent {
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint8 state;
	Uint8 repeat;
	SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	char text[SDL_TEXTINPUTEVENT_TEXT_SIZE];
} SDL_TextInputEvent;

typedef struct SDL_MouseButtonEvent {
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Uint8 button;
	Uint8 state;
	Uint8 clicks;
	Sint32 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Sint32 x, y;
} SDL_MouseWheelEvent;

typedef struct SDL_JoyAxisEvent {
	Uint32 type;
	Uint32 timestamp;
	Sint32 which;
	Uint8 axis;
	Sint16 value;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyButtonEvent {
	Uint32 type;
	Uint32 timestamp;
	Sint32 which;
	Uint8 button;
	Uint8 state;
} SDL_JoyButtonEvent;

typedef struct SDL_ControllerAxisEvent {
	Uint32 type;
	Uint32 timestamp;
	Sint32 which;
	Uint8 axis;
	Sint16 value;
} SDL_ControllerAxisEvent;

typedef struct SDL_ControllerButtonEvent {
	Uint32 type;
	Uint32 timestamp;
	Sint32 which;
	Uint8 button;
	Uint8 state;
} SDL_ControllerButtonEvent;

typedef struct SDL_ControllerDeviceEvent {
	Uint32 type;
	Uint32 timestamp;
	Sint32 which;
} SDL_ControllerDeviceEvent;

typedef struct SDL_UserEvent {
	Uint32 type;
	Uint32 timestamp;
	Uint32 windowID;
	Sint32 code;
	void *data1;
	void *data2;
} SDL_UserEvent;

typedef union SDL_Event {
	Uint32 type;
	SDL_CommonEvent common;
	SDL_WindowEvent window;
	SDL_KeyboardEvent key;
	SDL_TextInputEvent text;
	SDL_MouseButtonEvent button;
	SDL_MouseWheelEvent wheel;
	SDL_JoyAxisEvent jaxis;
	SDL_JoyButtonEvent jbutton;
	SDL_ControllerAxisEvent caxis;
	SDL_ControllerButtonEvent cbutton;
	SDL_ControllerDeviceEvent cdevice;
	SDL_UserEvent user;
	Uint8 padding[64];
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
Uint32 SDL_GetMouseState(int *x, int *y);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);
void SDL_SetTextInputRect(const SDL_Rect *rect);

#define SDL_ENABLE  1
#define SDL_DISABLE 0
int SDL_ShowCursor(int toggle);

/* ── timing ──────────────────────────────────────────────────── */

typedef int SDL_TimerID;
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);

Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param);
SDL_bool SDL_RemoveTimer(SDL_TimerID id);

/* ── audio (bucket C -- stubbed behind the pop_audio_* seam) ─── */

typedef Uint16 SDL_AudioFormat;

#define AUDIO_U8     0x0008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB

typedef struct SDL_AudioSpec {
	int freq;
	SDL_AudioFormat format;
	Uint8 channels;
	Uint8 silence;
	Uint16 samples;
	Uint32 size;
	void (*callback)(void *userdata, Uint8 *stream, int len);
	void *userdata;
} SDL_AudioSpec;

typedef struct SDL_AudioCVT {
	int needed;
	SDL_AudioFormat src_format;
	SDL_AudioFormat dst_format;
	double rate_incr;
	Uint8 *buf;
	int len;
	int len_cvt;
	int len_mult;
	double len_ratio;
	void *filters[10];
	int filter_index;
} SDL_AudioCVT;

typedef enum {
	SDL_AUDIO_STOPPED = 0,
	SDL_AUDIO_PLAYING,
	SDL_AUDIO_PAUSED,
} SDL_AudioStatus;

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_CloseAudio(void);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
SDL_AudioStatus SDL_GetAudioStatus(void);
int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
		      SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
		      SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT *cvt);

/* ── RWops ───────────────────────────────────────────────────── */

#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2

typedef struct SDL_RWops SDL_RWops;
struct SDL_RWops {
	Sint64 (*size)(SDL_RWops *ctx);
	Sint64 (*seek)(SDL_RWops *ctx, Sint64 offset, int whence);
	size_t (*read)(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum);
	size_t (*write)(SDL_RWops *ctx, const void *ptr, size_t size, size_t num);
	int (*close)(SDL_RWops *ctx);
	Uint32 type;
	struct {
		Uint8 *base;
		Uint8 *here;
		Uint8 *stop;
	} mem;
	void *hidden;
};

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
size_t SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum);
size_t SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num);
Sint64 SDL_RWtell(SDL_RWops *ctx);
int SDL_RWclose(SDL_RWops *ctx);

/* ── joystick / controller / haptic (bucket D -- none present) ── */

typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Haptic SDL_Haptic;
typedef Sint32 SDL_JoystickID;

typedef enum {
	SDL_CONTROLLER_AXIS_LEFTX = 0,
	SDL_CONTROLLER_AXIS_LEFTY,
	SDL_CONTROLLER_AXIS_RIGHTX,
	SDL_CONTROLLER_AXIS_RIGHTY,
	SDL_CONTROLLER_AXIS_TRIGGERLEFT,
	SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
} SDL_GameControllerAxis;

typedef enum {
	SDL_CONTROLLER_BUTTON_A = 0,
	SDL_CONTROLLER_BUTTON_B,
	SDL_CONTROLLER_BUTTON_X,
	SDL_CONTROLLER_BUTTON_Y,
	SDL_CONTROLLER_BUTTON_BACK,
	SDL_CONTROLLER_BUTTON_GUIDE,
	SDL_CONTROLLER_BUTTON_START,
	SDL_CONTROLLER_BUTTON_LEFTSTICK,
	SDL_CONTROLLER_BUTTON_RIGHTSTICK,
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	SDL_CONTROLLER_BUTTON_DPAD_UP,
	SDL_CONTROLLER_BUTTON_DPAD_DOWN,
	SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
} SDL_GameControllerButton;

int SDL_NumJoysticks(void);
SDL_bool SDL_IsGameController(int joystick_index);
SDL_GameController *SDL_GameControllerOpen(int joystick_index);
void SDL_GameControllerClose(SDL_GameController *gamecontroller);
SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID joyid);
int SDL_GameControllerAddMappingsFromFile(const char *file);
int SDL_GameControllerRumble(SDL_GameController *gamecontroller,
			     Uint16 low_frequency_rumble, Uint16 high_frequency_rumble,
			     Uint32 duration_ms);
SDL_Joystick *SDL_JoystickOpen(int device_index);
int SDL_JoystickRumble(SDL_Joystick *joystick,
		       Uint16 low_frequency_rumble, Uint16 high_frequency_rumble,
		       Uint32 duration_ms);
SDL_Haptic *SDL_HapticOpen(int device_index);
int SDL_HapticRumbleInit(SDL_Haptic *haptic);
int SDL_HapticRumblePlay(SDL_Haptic *haptic, float strength, Uint32 length);
void SDL_GL_GetDrawableSize(SDL_Window *window, int *w, int *h);

/* ── messagebox ──────────────────────────────────────────────── */

#define SDL_MESSAGEBOX_ERROR 0x00000010u

int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message,
			     SDL_Window *window);

/* ── main redirection (SDLPoP's main() runs off the Zephyr main
 *    thread via shim_main.c) ──────────────────────────────────── */

#ifndef SDL_MAIN_HANDLED
#define main SDL_main
#endif
extern int SDL_main(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SDL2_SDL_H */
