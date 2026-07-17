/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- mini-UART key input (WO-T1 M2).
 *
 * Lets a plain serial terminal on the mini-UART drive the launcher
 * picker exactly like PiZZaBoot's menu -- no pad or CDC `retro key`
 * needed. Bytes are parsed into SDL scancodes and submitted as
 * KEYDOWN+KEYUP pairs through the shim event seam, so everything the
 * pad drives, the terminal drives too (menus latch per KEYDOWN; games
 * see a tap, not a hold).
 *
 * Map: ESC[A/B/C/D -> UP/DOWN/RIGHT/LEFT, Enter -> RETURN (launch),
 * x -> X (retropad A), bare ESC -> ESCAPE (Home/back).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

#include "sdl2shim.h"

#if DT_NODE_EXISTS(DT_NODELABEL(uart1))

#define UART_NODE DT_NODELABEL(uart1)
#define ESC_TIMEOUT_MS 60

static void submit_tap(SDL_Scancode sc)
{
	SDL_Event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = SDL_KEYDOWN;
	ev.key.state = SDL_PRESSED;
	ev.key.keysym.scancode = sc;
	s2s_event_submit(&ev);

	ev.type = SDL_KEYUP;
	ev.key.state = SDL_RELEASED;
	s2s_event_submit(&ev);
}

static void uart_input_thread(void *a, void *b, void *c)
{
	const struct device *dev = DEVICE_DT_GET(UART_NODE);
	int esc = 0;
	int64_t esc_t = 0;
	unsigned char ch;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	if (!device_is_ready(dev)) {
		return;
	}

	for (;;) {
		while (uart_poll_in(dev, &ch) == 0) {
			if (esc == 1) {
				if (ch == '[') {
					esc = 2;
					continue;
				}
				esc = (ch == 0x1b) ? 1 : 0;
				continue;
			}
			if (esc == 2) {
				esc = 0;
				switch (ch) {
				case 'A':
					submit_tap(SDL_SCANCODE_UP);
					break;
				case 'B':
					submit_tap(SDL_SCANCODE_DOWN);
					break;
				case 'C':
					submit_tap(SDL_SCANCODE_RIGHT);
					break;
				case 'D':
					submit_tap(SDL_SCANCODE_LEFT);
					break;
				default:
					break;
				}
				continue;
			}
			if (ch == 0x1b) {
				esc = 1;
				esc_t = k_uptime_get();
				continue;
			}
			if (ch == '\r' || ch == '\n') {
				submit_tap(SDL_SCANCODE_RETURN);
			} else if (ch == 'x' || ch == 'X') {
				submit_tap(SDL_SCANCODE_X);
			}
		}

		if (esc == 1 && (k_uptime_get() - esc_t) > ESC_TIMEOUT_MS) {
			esc = 0;
			submit_tap(SDL_SCANCODE_ESCAPE);
		}
		k_msleep(10);
	}
}

K_THREAD_DEFINE(retro_uart_in, 1024, uart_input_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(12), 0, 0);

#endif /* uart1 */
