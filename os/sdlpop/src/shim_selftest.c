/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDLPoP port -- video-path self-test
 * (CONFIG_SDLPOP_VIDEO_SELFTEST).
 *
 * Runs before the game starts and drives the exact API path SDLPoP's
 * update_screen() uses -- CreateWindow/CreateRenderer, a 24bpp
 * surface composed with SDL_FillRect + direct pixel writes,
 * UpdateTexture, RenderCopy, RenderPresent. Output: classic color
 * bars + a gray ramp + a 16-step strip. On hardware this paints the
 * HDMI panel via the HVS-scaled fb (phase-6 eyeball); on sim the
 * capture backend dumps it for the phase-2 gate.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pop_shim.h"

LOG_MODULE_REGISTER(pop_selftest, CONFIG_SDLPOP_LOG_LEVEL);

/* SDLPoP's LE 24bpp masks (types.h): R low byte. */
#define POP_RMSK 0x000000FF
#define POP_GMSK 0x0000FF00
#define POP_BMSK 0x00FF0000

void pop_video_selftest(void)
{
	LOG_INF("selftest: video path");

	SDL_Window *win = SDL_CreateWindow("selftest", SDL_WINDOWPOS_UNDEFINED,
					   SDL_WINDOWPOS_UNDEFINED, 320, 200, 0);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
	SDL_Surface *surf = SDL_CreateRGBSurface(0, 320, 200, 24,
						 POP_RMSK, POP_GMSK, POP_BMSK, 0);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
					     SDL_TEXTUREACCESS_STREAMING, 320, 200);

	if (ren == NULL || surf == NULL || tex == NULL) {
		LOG_ERR("selftest: setup failed: %s", SDL_GetError());
		return;
	}

	/* Rows 0..139: 8 color bars (white yellow cyan green magenta
	 * red blue black), 40 px each.
	 */
	static const Uint8 bars[8][3] = {
		{ 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
		{ 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 0, 0, 0 },
	};

	for (int i = 0; i < 8; i++) {
		SDL_Rect r = { i * 40, 0, 40, 140 };
		Uint32 c = SDL_MapRGB(surf->format, bars[i][0], bars[i][1], bars[i][2]);

		SDL_FillRect(surf, &r, c);
	}

	/* Rows 140..169: horizontal gray ramp, direct pixel writes
	 * through Lock/Unlock like decode_image does.
	 */
	SDL_LockSurface(surf);
	for (int y = 140; y < 170; y++) {
		Uint8 *px = (Uint8 *)surf->pixels + (size_t)y * surf->pitch;

		for (int x = 0; x < 320; x++) {
			Uint8 v = (Uint8)((x * 255) / 319);

			px[0] = v;
			px[1] = v;
			px[2] = v;
			px += 3;
		}
	}
	SDL_UnlockSurface(surf);

	/* Rows 170..199: 16-step strip (VGA-palette-ish hues). */
	for (int i = 0; i < 16; i++) {
		SDL_Rect r = { i * 20, 170, 20, 30 };
		Uint32 c = SDL_MapRGB(surf->format,
				      (Uint8)(i * 17),
				      (Uint8)(255 - i * 17),
				      (Uint8)((i & 3) * 85));

		SDL_FillRect(surf, &r, c);
	}

	SDL_UpdateTexture(tex, NULL, surf->pixels, surf->pitch);
	SDL_RenderCopy(ren, tex, NULL, NULL);
	SDL_RenderPresent(ren);

	/* Hold the bars on the panel long enough to eyeball -- this is the
	 * phase-6 sign-off for the present path and, crucially, R/B channel
	 * order (leftmost bar must read WHITE then the color order
	 * white/yellow/cyan/green/magenta/red/blue/black). Re-present a few
	 * times across the hold so a dropped frame can't leave it blank.
	 */
	LOG_INF("selftest: holding color bars %d ms -- check leftmost bar is WHITE, "
		"then yellow/cyan/green/magenta/RED/BLUE/black",
		CONFIG_SDLPOP_SELFTEST_HOLD_MS);
	for (int elapsed = 0; elapsed < CONFIG_SDLPOP_SELFTEST_HOLD_MS; elapsed += 500) {
		SDL_UpdateTexture(tex, NULL, surf->pixels, surf->pitch);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(500);
	}

	SDL_FreeSurface(surf);
	LOG_INF("selftest: color bars done");
}
