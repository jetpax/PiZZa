/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa Phase 6 — CDC-transfer loader spike (rpi_zero_2w).
 *
 * Proves the picotool-style upload path without MSC:
 *   host upload.py --(USB CDC)--> firmware --> write /SD:/sketch.llext
 *   --> llext_load + run, in-process (no reboot, no SD hand-off).
 *
 * The firmware owns the SD card the entire time, so the host never
 * fights it for the card -- the whole MSC mode-switch problem is gone,
 * and a real sketch could still use the SD for its own Storage.
 *
 * Wire protocol (host -> device): "PZUP" + <le32 length> + <length bytes>.
 * Device replies "OK\n" (or "ERR ...\n") on the same CDC line, logs to UART.
 */

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/fs_loader.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define SKETCH_PATH "/SD:/sketch.llext"
#define UP_MAGIC    "PZUP"

static const struct device *const cdc = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
RING_BUF_DECLARE(rx_rb, 8192);
static struct usbd_context *usbd;

/* CDC RX ISR: drain the FIFO into the ring buffer. */
static void cdc_isr(const struct device *dev, void *user)
{
	ARG_UNUSED(user);

	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev)) {
		uint8_t tmp[64];
		int n = uart_fifo_read(dev, tmp, sizeof(tmp));

		if (n <= 0) {
			break;
		}
		ring_buf_put(&rx_rb, tmp, n);
	}
}

/* Block until exactly len bytes have been pulled from the ring buffer. */
static void rx_exact(uint8_t *dst, size_t len)
{
	size_t got = 0;

	while (got < len) {
		uint32_t r = ring_buf_get(&rx_rb, dst + got, len - got);

		got += r;
		if (r == 0) {
			k_msleep(2);
		}
	}
}

static void cdc_print(const char *s)
{
	for (; *s != '\0'; s++) {
		uart_poll_out(cdc, (uint8_t)*s);
	}
}

/* Receive one framed sketch over CDC and write it to the SD card. */
static int recv_sketch(void)
{
	uint8_t hdr[8];

	rx_exact(hdr, sizeof(hdr));
	if (memcmp(hdr, UP_MAGIC, 4) != 0) {
		LOG_ERR("bad magic: %02x %02x %02x %02x", hdr[0], hdr[1], hdr[2], hdr[3]);
		cdc_print("ERR magic\n");
		return -EINVAL;
	}

	uint32_t len = sys_get_le32(&hdr[4]);

	LOG_INF("receiving %u bytes over CDC -> %s", len, SKETCH_PATH);

	struct fs_file_t f;

	fs_file_t_init(&f);
	int rc = fs_open(&f, SKETCH_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);

	if (rc) {
		LOG_ERR("fs_open(%s) = %d", SKETCH_PATH, rc);
		cdc_print("ERR open\n");
		return rc;
	}

	uint32_t left = len;
	uint8_t buf[256];

	while (left > 0) {
		uint32_t chunk = MIN(left, sizeof(buf));

		rx_exact(buf, chunk);
		ssize_t w = fs_write(&f, buf, chunk);

		if (w < 0) {
			LOG_ERR("fs_write = %d", (int)w);
			fs_close(&f);
			cdc_print("ERR write\n");
			return (int)w;
		}
		left -= chunk;
	}

	fs_close(&f);
	LOG_INF("wrote %u bytes to %s", len, SKETCH_PATH);
	cdc_print("OK\n");
	return 0;
}

/* The loaded sketch runs in its own thread so the loader keeps control
 * (to watch for the 1200-bps upload touch while the sketch loops forever). */
static struct llext *cur_ext;
static K_THREAD_STACK_DEFINE(sketch_stack, 16384);
static struct k_thread sketch_thread;
static bool sketch_active;

static void sketch_entry(void *entry, void *b, void *c)
{
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	llext_bootstrap(cur_ext, (llext_entry_fn_t)entry, NULL);
}

/* Load /SD:/sketch.llext and start it in a thread. */
static int start_sketch(void)
{
	struct llext_fs_loader fsl = LLEXT_FS_LOADER(SKETCH_PATH);
	struct llext_load_param parm = LLEXT_LOAD_PARAM_DEFAULT;
	int rc = llext_load(&fsl.loader, "sketch", &cur_ext, &parm);

	if (rc) {
		LOG_ERR("llext_load = %d", rc);
		cur_ext = NULL;
		return rc;
	}

	void *main_fn = llext_find_sym(&cur_ext->exp_tab, "main");

	if (main_fn == NULL) {
		LOG_ERR("sketch has no 'main'");
		llext_unload(&cur_ext);
		cur_ext = NULL;
		return -ENOENT;
	}

	k_thread_create(&sketch_thread, sketch_stack, K_THREAD_STACK_SIZEOF(sketch_stack),
			sketch_entry, main_fn, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&sketch_thread, "sketch");
	sketch_active = true;
	LOG_INF("sketch started (in its own thread)");
	return 0;
}

/* Abort the running sketch and free it -- all in-process, no reboot. */
static void stop_sketch(void)
{
	if (sketch_active) {
		k_thread_abort(&sketch_thread);
		sketch_active = false;
	}
	if (cur_ext != NULL) {
		int rc = llext_unload(&cur_ext);

		LOG_INF("sketch stopped + unloaded (rc %d)", rc);
		cur_ext = NULL;
	}
}

static uint32_t cdc_baud(void)
{
	uint32_t baud = 0;

	(void)uart_line_ctrl_get(cdc, UART_LINE_CTRL_BAUD_RATE, &baud);
	return baud;
}

int main(void)
{
	if (!device_is_ready(cdc)) {
		LOG_ERR("CDC device not ready");
		return -ENODEV;
	}

	usbd = sample_usbd_init_device(NULL);
	if (usbd == NULL || usbd_enable(usbd) != 0) {
		LOG_ERR("USB device enable failed");
		return -ENODEV;
	}

	uart_irq_callback_set(cdc, cdc_isr);
	uart_irq_rx_enable(cdc);

	LOG_INF("CDC-loader runtime spike ready (firmware owns SD; no MSC, no reboot).");
	LOG_INF("Upload: upload.py /dev/cu.usbmodem* <sketch.llext>  (does a 1200bps touch)");

	/* Auto-run the sketch already on the SD (POR persistence). */
	struct fs_dirent ent;

	if (fs_stat(SKETCH_PATH, &ent) == 0) {
		LOG_INF("auto-loading %s (%u bytes) from SD", SKETCH_PATH, (unsigned int)ent.size);
		start_sketch();
	} else {
		LOG_INF("no %s yet -- upload one", SKETCH_PATH);
	}

	/* Watch the CDC for the IDE's 1200-bps "enter upload" touch while the
	 * sketch loops in its own thread. On the touch: swap the sketch
	 * in-process (stop -> receive new -> start). */
	uint32_t prev = cdc_baud();

	for (;;) {
		uint32_t baud = cdc_baud();

		if (baud == 1200 && prev != 1200) {
			LOG_INF("1200-bps touch detected -> upload mode (swapping sketch in-process)");
			stop_sketch();
			if (recv_sketch() == 0) {
				start_sketch();
			}
			prev = cdc_baud();
			continue;
		}
		prev = baud;
		k_msleep(30);
	}
}
