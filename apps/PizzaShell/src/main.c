/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa -- one Zephyr "neofetch" sample for the whole Raspberry Pi Zero
 * family. The `pizza about` menu lists the same fields on every board;
 * each value is filled in where the platform supports it and shows "--"
 * where it does not, so the menu reads consistently across boards:
 *
 *   - rpi_zero_w  (BCM2835, ARM1176, ARMv6) -- console-only port: SoC,
 *     memory, console, uptime are real; storage/display/Wi-Fi/temp "--".
 *   - rpi_zero_2w (BCM2710, Cortex-A53, AArch64) -- full image: USB-CDC
 *     shell, HDMI, microSD, CYW43439 Wi-Fi, VideoCore die-temp.
 *
 * Every platform-specific field/subsystem is compile-time gated, so the
 * single source builds clean against a minimal board and lights up the
 * extra fields automatically when a board enables the backing driver.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <version.h>
#include <stdio.h>
#include <string.h>

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)
#include <zephyr/usb/usbd.h>
#include <sample_usbd.h>
#endif
#if DT_HAS_CHOSEN(zephyr_display)
#include <zephyr/drivers/display.h>
#endif
#if IS_ENABLED(CONFIG_RASPBERRYPI_FIRMWARE)
#include <rpi_fw.h>
#endif
#if IS_ENABLED(CONFIG_SENSOR)
#include <zephyr/drivers/sensor.h>
#endif
#if IS_ENABLED(CONFIG_HWINFO)
#include <zephyr/drivers/hwinfo.h>
#endif
#if IS_ENABLED(CONFIG_FILE_SYSTEM)
#include <zephyr/fs/fs.h>
#endif
#if IS_ENABLED(CONFIG_WIFI)
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#endif

#define PIZZA_VERSION "v0.4.1"

/* Per-board identity + the static "info" lines. */
#if defined(CONFIG_SOC_BCM2835)
#define PIZZA_BOARD_NAME  "Raspberry Pi Zero W"
#define PIZZA_SOC_STR     "Broadcom BCM2835 (ARM1176JZF-S, ARMv6KZ AArch32)"
#elif defined(CONFIG_SOC_SUN50I_H618)
#define PIZZA_BOARD_NAME  "Transpeed FX-H618-D4"
#define PIZZA_SOC_STR     "Allwinner H618 (Cortex-A53 quad, ARMv8-A AArch64)"
#else
#define PIZZA_BOARD_NAME  "Raspberry Pi Zero 2 W"
#define PIZZA_SOC_STR     "Broadcom BCM2710 (Cortex-A53 quad, ARMv8-A AArch64)"
#endif

/*
 * Console identity. With CDC ACM the shell moves to USB and the UART
 * carries logs alone; otherwise the two share one port and there is no
 * separate logs channel to name. The STB brings no USB device port out,
 * so it is always the plain-UART case.
 */
#if defined(CONFIG_SOC_SUN50I_H618)
#define PIZZA_CONSOLE_STR "uart0 (dw-apb) on the board header (you're here)"
#define PIZZA_LOGS_STR    "--"
#elif IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS)
#define PIZZA_CONSOLE_STR "USB CDC ACM (you're here)"
#define PIZZA_LOGS_STR    "PL011 / mini-UART on GPIO 14/15"
#else
#define PIZZA_CONSOLE_STR "AUX mini-UART on GPIO 14/15 (you're here)"
#define PIZZA_LOGS_STR    "--"
#endif

#define BANNER_TITLE \
	"\x1b[1;32mPiZZa " PIZZA_VERSION " -- Zephyr v" KERNEL_VERSION_STRING \
	" on " PIZZA_BOARD_NAME "\x1b[0m\r\n"
#define BANNER_HELP "Type 'help' for more information.\r\n"

/* Boot-time and shell-command form: no screen clear, no fake prompt. */
static const char banner[] = "\r\n" BANNER_TITLE BANNER_HELP;

static int cmd_pizza_welcome(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_fprintf(sh, SHELL_NORMAL, "%s", banner);
	return 0;
}

/* ----------------------------------------------------------------- *
 *  USB CDC ACM banner-on-connect (boards with the USB device stack)
 * ----------------------------------------------------------------- */
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)

/*
 * CDC-connect form: clear screen, title + help, then a "uart:~$ "
 * placeholder in bold green matching the shell's real prompt colour.
 * The first host keystroke hands control to the real shell, which
 * re-renders ITS prompt in the same colour/position -- seam invisible.
 *
 * Why fake: the shell TX path can only be driven safely from the
 * shell's own thread or a shell-command callback; a write from the
 * system workqueue blocks forever on the TX lock/flush.
 */
static const char banner_cdc[] =
	"\x1b[2J\x1b[H" BANNER_TITLE BANNER_HELP "\x1b[1;32muart:~$ \x1b[0m";

static void welcome_write_banner(void)
{
	const struct device *cdc = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

	if (!device_is_ready(cdc)) {
		return;
	}
	for (const char *p = banner_cdc; *p; p++) {
		uart_poll_out(cdc, (uint8_t)*p);
	}
}

static void welcome_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	static int64_t last_fire_ms;
	int64_t now = k_uptime_get();

	/* USBD fires line-state / line-coding events repeatedly during
	 * enumeration; debounce so the banner prints once per port-open.
	 */
	if (now - last_fire_ms < 500) {
		return;
	}
	last_fire_ms = now;
	welcome_write_banner();
}
static K_WORK_DELAYABLE_DEFINE(welcome_work, welcome_work_fn);

static void pizza_usbd_msg_cb(struct usbd_context *const ctx,
			      const struct usbd_msg *const msg)
{
	ARG_UNUSED(ctx);

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		(void)uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			k_work_reschedule(&welcome_work, K_MSEC(300));
		}
	} else if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		k_work_reschedule(&welcome_work, K_MSEC(300));
	}
}

static int pizza_usb_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(pizza_usbd_msg_cb);

	if (usbd == NULL) {
		printk("[pizza] sample_usbd_init_device failed\n");
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(usbd)) {
		int err = usbd_enable(usbd);

		if (err) {
			printk("[pizza] usbd_enable failed: %d\n", err);
			return err;
		}
	}
	return 0;
}
#else
static inline int pizza_usb_start(void) { return 0; }
#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

/* ----------------------------------------------------------------- *
 *  HDMI splash (boards with a chosen display)
 * ----------------------------------------------------------------- */
#if DT_HAS_CHOSEN(zephyr_display)

/*
 * Splash the HDMI scanout with a test card at boot, at whatever
 * resolution the display reports. Each element diagnoses a different
 * scanout fault by eye:
 *   - white 8px border: clipping, overscan, geometry
 *   - corner squares (TL red, TR green, BL blue, BR white): origin,
 *     mirroring and pitch errors, which show as shear or as a wrapped
 *     corner
 *   - eight vertical colour bars: pixel format and channel order
 *   - horizontal grayscale gradient: bit depth, banding, RGB skew
 * The border and corners are absolute sizes, not fractions, so the
 * finer the mode the tighter a clipping fault they catch.
 *
 * Rows are rendered one at a time into a static row buffer. 1920 covers
 * every Pi HDMI mode VC hands us; the buffer is static BSS so a fault
 * here doesn't blow main's stack.
 */
#define PIZZA_DISPLAY_MAX_W 1920U
#define PIZZA_DISPLAY_BORDER 8U
#define PIZZA_DISPLAY_CORNER 64U
static uint32_t pizza_display_row[PIZZA_DISPLAY_MAX_W];

static const uint32_t pizza_display_bars[] = {
	0xFFFFFFFFU, /* White */
	0xFFFFFF00U, /* Yellow */
	0xFF00FFFFU, /* Cyan */
	0xFF00FF00U, /* Green */
	0xFFFF00FFU, /* Magenta */
	0xFFFF0000U, /* Red */
	0xFF0000FFU, /* Blue */
	0xFF000000U, /* Key (black) */
};

/*
 * EDID-based monitor detection. config.txt's `hdmi_force_hotplug=1`
 * makes VC bring up a 640x480 fallback framebuffer even when no monitor
 * is attached, so `device_is_ready()` alone can't see through it.
 * Asking VC for EDID block 0 returns status == 0 only when a real
 * monitor is on the cable.
 */
static bool pizza_display_has_monitor(void)
{
#if IS_ENABLED(CONFIG_RASPBERRYPI_FIRMWARE)
	const struct device *fw = DEVICE_DT_GET_ONE(raspberrypi_bcm283x_firmware);
	struct {
		uint32_t block;
		uint32_t status;
		uint8_t edid[128];
	} req = {0};

	if (!device_is_ready(fw)) {
		return true;
	}
	if (rpi_fw_transfer(fw, RPI_FW_TAG_GET_EDID_BLOCK, &req, sizeof(req)) != 0) {
		return true;
	}
	return req.status == 0;
#else
	return true;
#endif
}

static void pizza_display_paint(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities caps;
	struct display_buffer_descriptor desc;
	uint16_t w, h, gband_top, gband_bot;
	const size_t n = ARRAY_SIZE(pizza_display_bars);

	if (!device_is_ready(dev) || !pizza_display_has_monitor()) {
		return;
	}
	display_get_capabilities(dev, &caps);
	w = caps.x_resolution;
	h = caps.y_resolution;

	if (w == 0 || h == 0 || w > PIZZA_DISPLAY_MAX_W) {
		return;
	}

	desc.buf_size = (size_t)w * sizeof(uint32_t);
	desc.width    = w;
	desc.height   = 1;
	desc.pitch    = w;

	/* The gradient occupies the third quarter of the height, so the
	 * bars are read against a flat field above it and a ramp below.
	 */
	gband_top = h / 2U;
	gband_bot = 3U * h / 4U;

	for (uint16_t y = 0; y < h; y++) {
		for (uint16_t x = 0; x < w; x++) {
			uint32_t px;

			if (y < PIZZA_DISPLAY_BORDER ||
			    y >= h - PIZZA_DISPLAY_BORDER ||
			    x < PIZZA_DISPLAY_BORDER ||
			    x >= w - PIZZA_DISPLAY_BORDER) {
				px = 0xFFFFFFFFU;
			} else if (y < PIZZA_DISPLAY_CORNER &&
				   x < PIZZA_DISPLAY_CORNER) {
				px = 0xFFFF0000U;
			} else if (y < PIZZA_DISPLAY_CORNER &&
				   x >= w - PIZZA_DISPLAY_CORNER) {
				px = 0xFF00FF00U;
			} else if (y >= h - PIZZA_DISPLAY_CORNER &&
				   x < PIZZA_DISPLAY_CORNER) {
				px = 0xFF0000FFU;
			} else if (y >= h - PIZZA_DISPLAY_CORNER &&
				   x >= w - PIZZA_DISPLAY_CORNER) {
				px = 0xFFFFFFFFU;
			} else if (y >= gband_top && y < gband_bot) {
				const uint32_t g = ((uint32_t)x * 255U) / w;

				px = 0xFF000000U | (g << 16) | (g << 8) | g;
			} else {
				px = pizza_display_bars[((size_t)x * n) / w];
			}
			pizza_display_row[x] = px;
		}
		(void)display_write(dev, 0, y, &desc, pizza_display_row);
	}
}
#else
static inline void pizza_display_paint(void) { }
#endif /* DT_HAS_CHOSEN(zephyr_display) */

int main(void)
{
	/* First print: board console (mini-UART logs path). */
	printk("%s", banner);

	/* Splash the test card on HDMI (no-op without a chosen display). */
	pizza_display_paint();

	/* Bring up USB CDC + DTR banner callback (no-op without USB). */
	(void)pizza_usb_start();
	return 0;
}

/* ----------------------------------------------------------------- *
 *  `pizza` shell command set
 * ----------------------------------------------------------------- */

/*
 * Peace-symbol ASCII art (jetpax = pax = peace), rendered in 256-colour
 * purple bold. The right-hand info column is positioned by snapping the
 * cursor back up 13 rows and right NF_LOGO_WIDTH columns.
 */
#define NF_LOGO_WIDTH  29
static const char nf_logo[] =
"\r\n\x1b[38;5;135;1m"
"        -+#%@@@%#+-      \r\n"
"      %@@@@@@@@@@@@@%    \r\n"
"    =@@@%* -@@@- *%@@@=  \r\n"
"   *@@@     @@@     @@@% \r\n"
"  +@@%      @@@      %@@+\r\n"
"  @@@     .#@@@#.     @@@\r\n"
"  @@@    @@@@@@@@@    @@@\r\n"
"  *@@# .@@* @@@ *@@. #@@*\r\n"
"   #@@@@@   @@@   @@@@@# \r\n"
"    *@@@@_ _@@@_ _@@@@*  \r\n"
"      #@@@@@@@@@@@@@#    \r\n"
"        *+%@@@@@%+*      \r\n"
"\r\n\x1b[0;37m";

static const char nf_color_bar[] =
"\x1b[40m   \x1b[41m   \x1b[42m   \x1b[43m   "
"\x1b[44m   \x1b[45m   \x1b[46m   \x1b[47m   \x1b[0m";

/* Snapshot the dynamic facts into stack buffers up front so the
 * rendering pass is pure formatting. Each writes "--" on platforms
 * where the backing subsystem is not compiled in.
 */
static void pizza_snapshot_temp(char *buf, size_t len)
{
#if IS_ENABLED(CONFIG_SENSOR)
	const struct device *thermal =
		DEVICE_DT_GET_ANY(raspberrypi_bcm2835_vc_thermal);
	struct sensor_value t;

	if (thermal == NULL || !device_is_ready(thermal) ||
	    sensor_sample_fetch(thermal) != 0 ||
	    sensor_channel_get(thermal, SENSOR_CHAN_DIE_TEMP, &t) != 0) {
		strncpy(buf, "(n/a)", len);
		buf[len - 1] = '\0';
		return;
	}
	snprintf(buf, len, "%d.%02d C", t.val1,
		 t.val2 < 0 ? -t.val2 / 10000 : t.val2 / 10000);
#else
	strncpy(buf, "--", len);
	buf[len - 1] = '\0';
#endif
}

static void pizza_snapshot_uptime(char *buf, size_t len)
{
	uint64_t s_total = k_uptime_get() / 1000;
	uint32_t d = s_total / 86400U;
	uint32_t h = (s_total / 3600U) % 24U;
	uint32_t m = (s_total / 60U) % 60U;
	uint32_t s = s_total % 60U;

	snprintf(buf, len, "%ud %uh %um %us", d, h, m, s);
}

static void pizza_snapshot_storage(char *buf, size_t len)
{
#if IS_ENABLED(CONFIG_FILE_SYSTEM)
	struct fs_statvfs st;
	int rc = fs_statvfs("/SD:", &st);

	if (rc < 0) {
		snprintf(buf, len, "(SD not mounted: %d)", rc);
		return;
	}

	/*
	 * f_blocks and f_bfree are counts of ALLOCATION UNITS, not sectors:
	 * Zephyr's FATFS backend fills them from f_getfree(), which counts
	 * clusters. The byte multiplier is therefore f_frsize (the cluster
	 * size), not f_bsize (the sector size) -- using f_bsize under-reports
	 * by the sectors-per-cluster factor, which is how a 510 MiB card
	 * showed up as 63 MiB at 8 sectors per cluster.
	 */
	uint64_t total_bytes = (uint64_t)st.f_frsize * (uint64_t)st.f_blocks;
	uint64_t free_bytes  = (uint64_t)st.f_frsize * (uint64_t)st.f_bfree;
	uint32_t total_mib   = (uint32_t)(total_bytes / (1024ULL * 1024ULL));
	uint32_t free_mib    = (uint32_t)(free_bytes  / (1024ULL * 1024ULL));

	if (total_mib >= 1024U) {
		snprintf(buf, len, "%u.%u / %u.%u GiB free @ /SD:",
			 free_mib / 1024U, ((free_mib % 1024U) * 10U) / 1024U,
			 total_mib / 1024U, ((total_mib % 1024U) * 10U) / 1024U);
	} else {
		snprintf(buf, len, "%u / %u MiB free @ /SD:", free_mib, total_mib);
	}
#else
	strncpy(buf, "--", len);
	buf[len - 1] = '\0';
#endif
}

static void pizza_snapshot_display(char *buf, size_t len)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities caps;

	if (!device_is_ready(dev) || !pizza_display_has_monitor()) {
		strncpy(buf, "(no monitor detected)", len);
		buf[len - 1] = '\0';
		return;
	}
	display_get_capabilities(dev, &caps);
	snprintf(buf, len, "HDMI %ux%u", caps.x_resolution, caps.y_resolution);
#else
	strncpy(buf, "--", len);
	buf[len - 1] = '\0';
#endif
}

static void pizza_snapshot_ipv4(char *buf, size_t len)
{
#if IS_ENABLED(CONFIG_WIFI)
	struct net_if *iface = net_if_get_first_wifi();
	struct net_in_addr *addr;

	if (iface == NULL) {
		strncpy(buf, "(no Wi-Fi iface)", len);
		buf[len - 1] = '\0';
		return;
	}
	addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (addr == NULL || addr->s_addr == 0U) {
		strncpy(buf, "(not connected)", len);
		buf[len - 1] = '\0';
		return;
	}
	if (net_addr_ntop(AF_INET, addr, buf, len) == NULL) {
		strncpy(buf, "(format error)", len);
		buf[len - 1] = '\0';
	}
#else
	strncpy(buf, "--", len);
	buf[len - 1] = '\0';
#endif
}

static int cmd_pizza_about(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);

	char temp_buf[24];
	char uptime_buf[32];
	char ip_buf[48];
	char mem_buf[24];
	char storage_buf[40];
	char display_buf[32];

	pizza_snapshot_temp(temp_buf, sizeof(temp_buf));
	pizza_snapshot_uptime(uptime_buf, sizeof(uptime_buf));
	pizza_snapshot_ipv4(ip_buf, sizeof(ip_buf));
	pizza_snapshot_storage(storage_buf, sizeof(storage_buf));
	pizza_snapshot_display(display_buf, sizeof(display_buf));
	snprintf(mem_buf, sizeof(mem_buf), "%llu MiB",
		 (unsigned long long)(DT_REG_SIZE(DT_CHOSEN(zephyr_sram)) /
				      (1024ULL * 1024ULL)));

	/* Logo. Cursor ends 13 rows below where it started. */
	shell_fprintf(sh, SHELL_NORMAL, "%s", nf_logo);

	/* Snap back to top-right of the logo for the info column. */
	shell_fprintf(sh, SHELL_NORMAL, "\x1b[13A\x1b[%dC", NF_LOGO_WIDTH);

#define IL(label, value) shell_fprintf(sh, SHELL_NORMAL, \
	"\x1b[1;31m%-9s\x1b[0;37m: %s\r\n\x1b[%dC", (label), (value), NF_LOGO_WIDTH)
	IL("SoC",      PIZZA_SOC_STR);
	IL("Memory",   mem_buf);
	IL("Storage",  storage_buf);
	IL("Display",  display_buf);
	IL("Local IP", ip_buf);
	IL("Console",  PIZZA_CONSOLE_STR);
	IL("Logs",     PIZZA_LOGS_STR);
	IL("Temp",     temp_buf);
	IL("Uptime",   uptime_buf);
	IL("Project",  "https://github.com/jetpax/PiZZa");
#undef IL

	/* Blank line + ANSI colour bar (still aligned right of the logo). */
	shell_fprintf(sh, SHELL_NORMAL, "\r\n\x1b[%dC%s\r\n", NF_LOGO_WIDTH, nf_color_bar);

	/* Push cursor below the logo so the next prompt clears the art. */
	shell_fprintf(sh, SHELL_NORMAL, "\r\n\r\n\r\n");
	return 0;
}

static int cmd_pizza_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);

#if IS_ENABLED(CONFIG_HWINFO)
	uint8_t id[16];
	ssize_t n = hwinfo_get_device_id(id, sizeof(id));

	if (n > 0) {
		shell_fprintf(sh, SHELL_NORMAL, "  Board serial   : 0x");
		for (ssize_t i = 0; i < n; i++) {
			shell_fprintf(sh, SHELL_NORMAL, "%02x", id[i]);
		}
		shell_print(sh, "");
	} else {
		shell_print(sh, "  Board serial   : (hwinfo unavailable)");
	}
#else
	shell_print(sh, "  Board serial   : --");
#endif

#if IS_ENABLED(CONFIG_SENSOR)
	const struct device *thermal = DEVICE_DT_GET_ANY(raspberrypi_bcm2835_vc_thermal);

	if (thermal && device_is_ready(thermal)) {
		struct sensor_value t;

		if (sensor_sample_fetch(thermal) == 0 &&
		    sensor_channel_get(thermal, SENSOR_CHAN_DIE_TEMP, &t) == 0) {
			shell_print(sh, "  Die temp       : %d.%02d C", t.val1,
				    t.val2 < 0 ? -t.val2 / 10000 : t.val2 / 10000);
		} else {
			shell_print(sh, "  Die temp       : (sensor fetch failed)");
		}
	} else {
		shell_print(sh, "  Die temp       : (sensor not ready)");
	}
#else
	shell_print(sh, "  Die temp       : --");
#endif

	shell_print(sh, "  Uptime         : %u ms", k_uptime_get_32());
	return 0;
}

static int cmd_pizza_wifi(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
#if IS_ENABLED(CONFIG_WIFI)
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		shell_warn(sh, "no Wi-Fi interface");
		return -ENODEV;
	}

	struct wifi_iface_status status = { 0 };
	int rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface,
			  &status, sizeof(status));
	if (rc < 0) {
		shell_warn(sh, "wifi status query failed (%d)", rc);
		return rc;
	}

	const char *state = (status.state >= WIFI_STATE_DISCONNECTED &&
			     status.state <= WIFI_STATE_COMPLETED) ?
				    wifi_state_txt(status.state) : "?";
	shell_print(sh, "  State          : %s", state);
	if (status.state >= WIFI_STATE_ASSOCIATED) {
		shell_print(sh, "  SSID           : %.*s",
			    status.ssid_len, status.ssid);
		shell_print(sh, "  BSSID          : %02x:%02x:%02x:%02x:%02x:%02x",
			    status.bssid[0], status.bssid[1], status.bssid[2],
			    status.bssid[3], status.bssid[4], status.bssid[5]);
		shell_print(sh, "  Channel        : %u", status.channel);
		shell_print(sh, "  RSSI           : %d dBm", status.rssi);
		shell_print(sh, "  Link mode      : %s",
			    wifi_link_mode_txt(status.link_mode));
		shell_print(sh, "  Security       : %s",
			    wifi_security_txt(status.security));
	} else {
		shell_print(sh, "  (not associated -- use `wifi connect -s <ssid> -p <psk> -k 1`)");
	}
	return 0;
#else
	shell_warn(sh, "Wi-Fi not supported on this platform");
	return -ENOTSUP;
#endif
}

static int cmd_pizza_temp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
#if IS_ENABLED(CONFIG_SENSOR)
	const struct device *thermal = DEVICE_DT_GET_ANY(raspberrypi_bcm2835_vc_thermal);

	if (!thermal || !device_is_ready(thermal)) {
		shell_warn(sh, "vc-thermal sensor not ready");
		return -ENODEV;
	}
	struct sensor_value t;
	int rc = sensor_sample_fetch(thermal);

	if (rc < 0) {
		shell_warn(sh, "fetch failed (%d)", rc);
		return rc;
	}
	rc = sensor_channel_get(thermal, SENSOR_CHAN_DIE_TEMP, &t);
	if (rc < 0) {
		shell_warn(sh, "get failed (%d)", rc);
		return rc;
	}
	shell_print(sh, "Die temperature: %d.%02d C",
		    t.val1, t.val2 < 0 ? -t.val2 / 10000 : t.val2 / 10000);
	return 0;
#else
	shell_warn(sh, "temperature sensor not supported on this platform");
	return -ENOTSUP;
#endif
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pizza,
	SHELL_CMD(about, NULL, "About PiZZa", cmd_pizza_about),
	SHELL_CMD(info,  NULL, "Board info (serial, temp, uptime)", cmd_pizza_info),
	SHELL_CMD(wifi,  NULL, "Wi-Fi association status", cmd_pizza_wifi),
	SHELL_CMD(temp,  NULL, "VideoCore die temperature", cmd_pizza_temp),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(pizza, &sub_pizza,
		   "PiZZa device summary (about / info / wifi / temp)",
		   cmd_pizza_about);

/* Top-level command used by main() to print the banner on USB-CDC DTR
 * rising edge. Kept separate from `pizza` to keep the subcommand list tidy.
 */
SHELL_CMD_REGISTER(welcome, NULL, "Print PiZZa banner", cmd_pizza_welcome);
