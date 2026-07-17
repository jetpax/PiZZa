/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZaBoot -- the Tier-1 GRUB-style boot menu (WO-T1 M1).
 *
 * The GPU firmware is the actual boot selector: config.txt defaults to
 * this menu, `include chosen.txt` overrides it with the persisted
 * choice, and holding the GPIO17 button at power-on forces the menu
 * back. This app only has to render a list, take a pick, write
 * chosen.txt (atomically -- bootsel_set_kernel), and reset.
 *
 * UI surfaces, all driven from one poll loop:
 *   - mini-UART (115200 on GPIO14/15) -- ANSI menu, always on
 *   - USB CDC ACM -- same menu, mirrored while DTR is up
 *   - HDMI -- 8x8-font list (hdmi.c), optional
 *   - GPIO17 button -- short press = next entry, long press = boot
 *
 * Entries come from menu.txt on the boot FAT ("Name = file.bin" lines,
 * plus "timeout =" / "default ="); fallback is a *.bin scan. A countdown
 * to the default entry runs only when no valid chosen.txt exists (fresh
 * card); when the menu was summoned over a valid choice (the button
 * path), it waits for the user.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootsel.h"
#include "pizzaboot.h"

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)
#include <zephyr/usb/usbd.h>
#include <sample_usbd.h>
#endif

#define MOUNT      "/SD:"
#define MENU_TXT   MOUNT "/menu.txt"
#define CHOSEN_TXT MOUNT "/chosen.txt"

#define DEFAULT_TIMEOUT_S 5
#define LONG_PRESS_MS     700
#define ESC_TIMEOUT_MS    60

/* --- boot entries ----------------------------------------------------------- */

static struct bootsel_entry entries[PB_MAX_ENTRIES];
static int entry_count;
static int menu_timeout_s = DEFAULT_TIMEOUT_S;
static char default_name[PB_NAME_LEN];
static char shell_file[PB_FILE_LEN];

/* --- console mux ------------------------------------------------------------ */

enum key {
	K_NONE = 0,
	K_UP,
	K_DOWN,
	K_ENTER,
	K_CMDLINE,		/* c: boot the "shell" kernel, GRUB-style */
	K_OTHER,		/* any other key: cancels the countdown */
	K_DIGIT_BASE = 100,	/* K_DIGIT_BASE + i = entry i */
};

struct con {
	const struct device *dev;
	bool cdc;
	bool active;
	int esc;		/* 0 idle, 1 got ESC, 2 got ESC[ */
	int64_t esc_t;
};

static struct con cons[2];
static int con_count;

static void con_putc(char c)
{
	for (int i = 0; i < con_count; i++) {
		if (!cons[i].active) {
			continue;
		}
		if (c == '\n') {
			uart_poll_out(cons[i].dev, '\r');
		}
		uart_poll_out(cons[i].dev, c);
	}
}

static void con_puts(const char *s)
{
	for (; *s != '\0'; s++) {
		con_putc(*s);
	}
}

static void __printf_like(1, 2) con_printf(const char *fmt, ...)
{
	char buf[192];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	con_puts(buf);
}

/* Throw away anything sitting in the RX FIFOs -- power-on line noise
 * lands there before the loop starts and must not cancel the countdown.
 */
static void con_drain(void)
{
	unsigned char junk;

	for (int i = 0; i < con_count; i++) {
		while (uart_poll_in(cons[i].dev, &junk) == 0) {
		}
	}
}

static enum key con_poll_key(struct con *c)
{
	unsigned char ch;

	while (uart_poll_in(c->dev, &ch) == 0) {
		if (c->esc == 1) {
			if (ch == '[') {
				c->esc = 2;
				continue;
			}
			c->esc = (ch == 0x1b) ? 1 : 0;
			continue;
		}
		if (c->esc == 2) {
			c->esc = 0;
			if (ch == 'A') {
				return K_UP;
			}
			if (ch == 'B') {
				return K_DOWN;
			}
			continue;
		}
		if (ch == 0x1b) {
			c->esc = 1;
			c->esc_t = k_uptime_get();
			continue;
		}
		if (ch == '\r' || ch == '\n') {
			return K_ENTER;
		}
		if (ch >= '1' && ch <= '9') {
			return (enum key)(K_DIGIT_BASE + (ch - '1'));
		}
		if (ch == 'c' || ch == 'C') {
			return K_CMDLINE;
		}
		/* 0x00/0xFF are power-on line glitches, not keys. */
		if (ch == 0x00 || ch == 0xFF) {
			continue;
		}
		return K_OTHER;
	}

	/* Bare ESC (no sequence followed): treat as "a key". */
	if (c->esc == 1 && (k_uptime_get() - c->esc_t) > ESC_TIMEOUT_MS) {
		c->esc = 0;
		return K_OTHER;
	}
	return K_NONE;
}

/* --- USB CDC ---------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)
static void usbd_cb(struct usbd_context *const ctx,
		    const struct usbd_msg *const msg)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(msg);
	/* DTR is polled from the main loop; nothing to do here. */
}

static void usb_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(usbd_cb);

	if (usbd == NULL) {
		printk("[pizzaboot] usbd init failed\n");
		return;
	}
	if (!usbd_can_detect_vbus(usbd)) {
		int err = usbd_enable(usbd);

		if (err != 0) {
			printk("[pizzaboot] usbd_enable: %d\n", err);
		}
	}
}

static bool cdc_dtr(const struct device *dev)
{
	uint32_t dtr = 0U;

	(void)uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
	return dtr != 0U;
}
#else
static void usb_start(void)
{
}
#endif

/* --- GPIO17 button ----------------------------------------------------------- */

static const struct device *gpio0_dev;
static bool btn_ok;
static bool btn_down;
static bool btn_swallow;
static int64_t btn_t0;

static void button_init(void)
{
	gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	if (!device_is_ready(gpio0_dev)) {
		printk("[pizzaboot] gpio0 not ready, button off\n");
		return;
	}
	if (gpio_pin_configure(gpio0_dev, 17, GPIO_INPUT) == 0) {
		btn_ok = true;
		/* If the button is still down from summoning the menu
		 * (the firmware [gpio17=0] path), that press belongs to
		 * the firmware, not to us: swallow it until released.
		 */
		btn_swallow = gpio_pin_get_raw(gpio0_dev, 17) == 0;
	}
}

/* Short press = K_DOWN (cycle), long press = K_ENTER (boot). The pin
 * idles high (firmware gpio=17=ip,pu; button to GND), so pressed == 0.
 */
static enum key button_poll(void)
{
	if (!btn_ok) {
		return K_NONE;
	}

	bool pressed = gpio_pin_get_raw(gpio0_dev, 17) == 0;
	int64_t now = k_uptime_get();

	if (btn_swallow) {
		if (!pressed) {
			btn_swallow = false;
		}
		return K_NONE;
	}

	if (pressed && !btn_down) {
		btn_down = true;
		btn_t0 = now;
	} else if (!pressed && btn_down) {
		btn_down = false;
		if (now - btn_t0 >= LONG_PRESS_MS) {
			return K_ENTER;
		}
		if (now - btn_t0 >= 30) {	/* debounce */
			return K_DOWN;
		}
	}
	return K_NONE;
}

/* --- entries ----------------------------------------------------------------- */

static bool str_ieq(const char *a, const char *b)
{
	for (; *a != '\0' && *b != '\0'; a++, b++) {
		if ((*a | 0x20) != (*b | 0x20)) {
			return false;
		}
	}
	return *a == *b;
}

/* Fallback when menu.txt is absent: every *.bin at the FAT root except
 * the GPU blob. Display name = filename minus .bin.
 */
static int scan_bins(void)
{
	struct fs_dir_t d;
	struct fs_dirent ent;
	int count = 0;

	fs_dir_t_init(&d);
	if (fs_opendir(&d, MOUNT) != 0) {
		return 0;
	}
	while (count < PB_MAX_ENTRIES && fs_readdir(&d, &ent) == 0 &&
	       ent.name[0] != '\0') {
		if (ent.type != FS_DIR_ENTRY_FILE || ent.name[0] == '.') {
			continue;
		}
		size_t len = strlen(ent.name);

		if (len < 5 || len >= PB_FILE_LEN ||
		    !str_ieq(ent.name + len - 4, ".bin")) {
			continue;
		}
		if (str_ieq(ent.name, "bootcode.bin")) {
			continue;
		}
		strncpy(entries[count].file, ent.name, PB_FILE_LEN - 1);
		memcpy(entries[count].name, ent.name,
		       MIN(len - 4, PB_NAME_LEN - 1));
		entries[count].present = true;
		count++;
	}
	fs_closedir(&d);
	return count;
}

/* --- rendering --------------------------------------------------------------- */

static void render(int sel, const char *status)
{
	con_puts("\x1b[2J\x1b[H");
	con_puts("PiZZaBoot -- select boot image\n\n");
	for (int i = 0; i < entry_count; i++) {
		con_printf("  %s%d. %-28s%s%s\n",
			   i == sel ? "\x1b[7m" : "",
			   i + 1, entries[i].name,
			   entries[i].present ? "" : " (missing)",
			   i == sel ? "\x1b[0m" : "");
	}
	if (entry_count == 0) {
		con_puts("  no boot entries found\n");
	}
	con_puts("\nup/down + enter, or a digit. button: short=next, long=boot\n");
	if (shell_file[0] != '\0') {
		con_puts("press c for a command line\n");
	}
	if (status != NULL && status[0] != '\0') {
		con_printf("%s\n", status);
	}
	hdmi_render(entries, entry_count, sel, shell_file[0] != '\0', status);
}

/* --- launch ------------------------------------------------------------------ */

static void launch(int idx, char *status, size_t status_sz)
{
	if (idx < 0 || idx >= entry_count) {
		return;
	}
	if (!entries[idx].present) {
		snprintf(status, status_sz, "%s: file missing",
			 entries[idx].name);
		return;
	}

	char msg[64];

	snprintf(msg, sizeof(msg), "booting %s ...", entries[idx].name);
	render(idx, msg);

	int rc = bootsel_set_kernel(entries[idx].file);

	if (rc != 0) {
		snprintf(status, status_sz, "chosen.txt write failed (%d)", rc);
		return;
	}
	k_msleep(50);
	bootsel_reboot();
}

static void launch_shell(int sel, char *status, size_t status_sz)
{
	char msg[64];

	if (shell_file[0] == '\0') {
		return;
	}
	snprintf(msg, sizeof(msg), "booting %s ...", shell_file);
	render(sel, msg);

	int rc = bootsel_set_kernel(shell_file);

	if (rc != 0) {
		snprintf(status, status_sz, "command line %s: error %d",
			 shell_file, rc);
		return;
	}
	k_msleep(50);
	bootsel_reboot();
}

/* --- main -------------------------------------------------------------------- */

int main(void)
{
	printk("[pizzaboot] WO-T1 M1\n");

	usb_start();
	button_init();
	(void)hdmi_init();

	cons[0].dev = DEVICE_DT_GET(DT_NODELABEL(uart1));
	cons[0].active = device_is_ready(cons[0].dev);
	con_count = 1;
#if DT_NODE_EXISTS(DT_NODELABEL(cdc_acm_uart0))
	cons[1].dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
	cons[1].cdc = true;
	cons[1].active = false;
	con_count = 2;
#endif

	/* The boot FAT is fstab-automounted; give a slow card a moment. */
	struct fs_dirent st;

	for (int i = 0; i < 20 && fs_stat(MENU_TXT, &st) == -ENOENT; i++) {
		if (fs_stat(MOUNT "/config.txt", &st) == 0) {
			break;
		}
		k_msleep(100);
	}

	entry_count = bootsel_load_menu(entries, PB_MAX_ENTRIES,
					&menu_timeout_s, default_name,
					sizeof(default_name),
					shell_file, sizeof(shell_file));
	if (entry_count == 0) {
		entry_count = scan_bins();
	}

	/* A valid chosen.txt means the firmware would have booted it --
	 * we are here because the button forced the menu, so no
	 * auto-boot countdown.
	 */
	bool countdown = !bootsel_chosen_valid() && entry_count > 0;
	int shown_secs = -1;

	int sel = 0;

	if (default_name[0] != '\0') {
		for (int i = 0; i < entry_count; i++) {
			if (str_ieq(entries[i].name, default_name)) {
				sel = i;
				break;
			}
		}
	}

	char status[64] = "";

	if (countdown && menu_timeout_s <= 0) {
		launch(sel, status, sizeof(status));
		countdown = false;
	}

	render(sel, status);
	con_drain();

	int64_t deadline = k_uptime_get() + (int64_t)menu_timeout_s * 1000;

#if DT_NODE_EXISTS(DT_NODELABEL(cdc_acm_uart0))
	uint32_t tick = 0;
#endif

	for (;;) {
		enum key k = K_NONE;

		for (int i = 0; i < con_count && k == K_NONE; i++) {
			if (cons[i].active) {
				k = con_poll_key(&cons[i]);
			}
		}
		if (k == K_NONE) {
			k = button_poll();
		}

#if DT_NODE_EXISTS(DT_NODELABEL(cdc_acm_uart0))
		/* DTR poll ~10 Hz: activate/deactivate the CDC mirror. */
		if ((tick++ % 10) == 0) {
			bool up = cdc_dtr(cons[1].dev);

			if (up && !cons[1].active) {
				cons[1].active = true;
				render(sel, status);
			} else if (!up && cons[1].active) {
				cons[1].active = false;
			}
		}
#endif

		if (k != K_NONE && countdown) {
			countdown = false;
			status[0] = '\0';
			render(sel, status);
		}

		switch ((int)k) {
		case K_UP:
			if (entry_count > 0) {
				sel = (sel + entry_count - 1) % entry_count;
				render(sel, status);
			}
			break;
		case K_DOWN:
			if (entry_count > 0) {
				sel = (sel + 1) % entry_count;
				render(sel, status);
			}
			break;
		case K_ENTER:
			status[0] = '\0';
			launch(sel, status, sizeof(status));
			render(sel, status);
			break;
		case K_CMDLINE:
			status[0] = '\0';
			launch_shell(sel, status, sizeof(status));
			render(sel, status);
			break;
		default:
			if (k >= K_DIGIT_BASE &&
			    k < K_DIGIT_BASE + entry_count) {
				sel = k - K_DIGIT_BASE;
				status[0] = '\0';
				launch(sel, status, sizeof(status));
				render(sel, status);
			}
			break;
		}

		if (countdown) {
			int64_t left = deadline - k_uptime_get();

			if (left <= 0) {
				launch(sel, status, sizeof(status));
				countdown = false;
				render(sel, status);
			} else {
				int secs = (int)((left + 999) / 1000);

				if (secs != shown_secs) {
					shown_secs = secs;
					snprintf(status, sizeof(status),
						 "booting %s in %d s (any key stops)",
						 entries[sel].name, secs);
					render(sel, status);
				}
			}
		}

		k_msleep(10);
	}
	return 0;
}
