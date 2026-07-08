#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>

#include "bthid_br.h"
#include "kbd_report.h"

/* HID profile PSMs (Bluetooth HID v1.1). */
#define PSM_HID_CTRL 0x0011
#define PSM_HID_INTR 0x0013

/* HID transaction header: DATA | Input report. */
#define HID_HDR_DATA_INPUT 0xa1

static struct bt_conn *hid_conn;
static struct kbd_state kbd;

static void hexdump(const char *tag, const uint8_t *p, uint16_t len)
{
	printk("%s (%u):", tag, len);
	for (uint16_t i = 0; i < len; i++) {
		printk(" %02x", p[i]);
	}
	printk("\n");
}

static void key_event(uint8_t usage, bool pressed, void *user_data)
{
	ARG_UNUSED(user_data);
	printk("    KEY 0x%02x %s\n", usage, pressed ? "DOWN" : "UP");
}

/* --- interrupt channel: input reports ------------------------------------ */

static int intr_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	const uint8_t *d = buf->data;
	uint16_t len = buf->len;

	hexdump("BR-HID INTR", d, len);

	if (len >= 2 && d[0] == HID_HDR_DATA_INPUT) {
		if (len - 1 == KBD_REPORT_LEN) {
			/* Boot-style: [0xA1][8-byte report] */
			kbd_report_process(&kbd, &d[1], KBD_REPORT_LEN,
					   key_event, NULL);
		} else if (len - 2 == KBD_REPORT_LEN) {
			/* Report protocol: [0xA1][report id][8-byte report] */
			printk("  (report id %u)\n", d[1]);
			kbd_report_process(&kbd, &d[2], KBD_REPORT_LEN,
					   key_event, NULL);
		}
	}
	return 0;
}

static int ctrl_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	hexdump("BR-HID CTRL", buf->data, buf->len);
	return 0;
}

/* --- channel lifecycle ----------------------------------------------------- */

static void intr_connected(struct bt_l2cap_chan *chan)
{
	printk("BR-HID: interrupt channel up -- press EVERY button (keymap "
	       "capture)\n");
}

static void intr_disconnected(struct bt_l2cap_chan *chan)
{
	printk("BR-HID: interrupt channel down\n");
	kbd_report_release_all(&kbd, key_event, NULL);
}

static void ctrl_connected(struct bt_l2cap_chan *chan);
static void ctrl_disconnected(struct bt_l2cap_chan *chan)
{
	printk("BR-HID: control channel down\n");
}

static const struct bt_l2cap_chan_ops intr_ops = {
	.recv = intr_recv,
	.connected = intr_connected,
	.disconnected = intr_disconnected,
};

static const struct bt_l2cap_chan_ops ctrl_ops = {
	.recv = ctrl_recv,
	.connected = ctrl_connected,
	.disconnected = ctrl_disconnected,
};

static struct bt_l2cap_br_chan ctrl_chan;
static struct bt_l2cap_br_chan intr_chan;

static void ctrl_connected(struct bt_l2cap_chan *chan)
{
	int err;

	printk("BR-HID: control channel up; opening interrupt channel\n");

	intr_chan.chan.ops = &intr_ops;
	intr_chan.rx.mtu = 64;

	err = bt_l2cap_chan_connect(hid_conn, &intr_chan.chan, PSM_HID_INTR);
	if (err) {
		printk("BR-HID: interrupt connect failed (%d)\n", err);
	}
}

/* --- inbound path: device-initiated reconnection --------------------------- */

static int hid_server_accept(struct bt_conn *conn,
			     struct bt_l2cap_server *server,
			     struct bt_l2cap_chan **chan)
{
	struct bt_l2cap_br_chan *c = (server->psm == PSM_HID_CTRL)
					     ? &ctrl_chan
					     : &intr_chan;

	if (hid_conn != NULL && hid_conn != conn) {
		return -ENOMEM;
	}

	if (hid_conn == NULL) {
		hid_conn = conn;
		memset(&kbd, 0, sizeof(kbd));
		printk("BR-HID: inbound reconnect (PSM 0x%04x)\n", server->psm);
	}

	/* Accept-callback contract: zero the channel object ourselves. */
	memset(c, 0, sizeof(*c));
	c->chan.ops = (server->psm == PSM_HID_CTRL) ? &ctrl_ops : &intr_ops;
	c->rx.mtu = 64;

	*chan = &c->chan;
	return 0;
}

static struct bt_l2cap_server ctrl_server = {
	.psm = PSM_HID_CTRL,
	.sec_level = BT_SECURITY_L2,
	.accept = hid_server_accept,
};

static struct bt_l2cap_server intr_server = {
	.psm = PSM_HID_INTR,
	.sec_level = BT_SECURITY_L2,
	.accept = hid_server_accept,
};

void bthid_br_register(void)
{
	int err;

	err = bt_l2cap_br_server_register(&ctrl_server);
	err |= bt_l2cap_br_server_register(&intr_server);
	if (err) {
		printk("BR-HID: server register failed\n");
		return;
	}
	printk("BR-HID: PSM 0x11/0x13 servers registered (inbound reconnect)\n");
}

/* --- public entry points ---------------------------------------------------- */

void bthid_br_start(struct bt_conn *conn)
{
	int err;

	/* Safe: while disconnected the stack holds no refs to these. */
	memset(&ctrl_chan, 0, sizeof(ctrl_chan));
	memset(&intr_chan, 0, sizeof(intr_chan));
	memset(&kbd, 0, sizeof(kbd));
	hid_conn = conn;

	ctrl_chan.chan.ops = &ctrl_ops;
	ctrl_chan.rx.mtu = 64;

	/* HID v1.1: control channel first, then interrupt. */
	err = bt_l2cap_chan_connect(conn, &ctrl_chan.chan, PSM_HID_CTRL);
	if (err) {
		printk("BR-HID: control connect failed (%d)\n", err);
		return;
	}
	printk("BR-HID: connecting control channel (PSM 0x11)...\n");
}

void bthid_br_stop(void)
{
	kbd_report_release_all(&kbd, key_event, NULL);
	hid_conn = NULL;
}

bool bthid_br_active(void)
{
	return hid_conn != NULL;
}
