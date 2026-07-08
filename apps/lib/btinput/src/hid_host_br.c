/*
 * apps/lib/btinput -- minimal Classic BT (BR/EDR) HID host.
 *
 * The 8BitDo Micro's keyboard (and most BT keyboards/mice) live on the
 * Classic HID profile. Zephyr ships BR/EDR ACL + SSP + L2CAP but no HID host
 * profile; this is the missing piece: L2CAP PSM 0x11 (control) + 0x13
 * (interrupt), input reports arrive on the interrupt channel as 0xA1-prefixed
 * HID reports.
 *
 * Bonded HID devices reconnect DEVICE-INITIATED -- they page the host and
 * open the L2CAP channels themselves -- so the host stays page-scannable with
 * both PSM servers registered and accepts the inbound link as CENTRAL (the
 * canonical HID topology; as peripheral the pad opens nothing). Extracted
 * from apps/BtK1/src/bthid_br.c (HW-proven 2026-07-08); see the memory
 * reference_zephyr_classic_hid_host.
 */

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/classic.h>

#include "btinput.h"
#include "kbd_report.h"

LOG_MODULE_REGISTER(btinput, CONFIG_BTINPUT_LOG_LEVEL);

/* HID profile PSMs (Bluetooth HID v1.1). */
#define PSM_HID_CTRL 0x0011
#define PSM_HID_INTR 0x0013

/* HID transaction header: DATA | Input report. */
#define HID_HDR_DATA_INPUT 0xa1

static struct bt_conn *hid_conn;
static struct kbd_state kbd;

/* Consumer-registered key sink (the SDL/termdirect seam, or a test print). */
static btinput_key_cb_t s_key_cb;
static void *s_key_user;

void btinput_set_key_cb(btinput_key_cb_t cb, void *user)
{
	s_key_cb = cb;
	s_key_user = user;
}

/* kbd_report parser trampoline -> the consumer sink. */
static void key_event(uint8_t usage, bool pressed, void *user_data)
{
	ARG_UNUSED(user_data);

	if (s_key_cb != NULL) {
		s_key_cb(usage, pressed, s_key_user);
	}
}

/* --- interrupt channel: input reports ------------------------------------ */

static int intr_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	const uint8_t *d = buf->data;
	uint16_t len = buf->len;

	LOG_HEXDUMP_DBG(d, len, "BR-HID INTR");

	if (len >= 2 && d[0] == HID_HDR_DATA_INPUT) {
		if (len - 1 == KBD_REPORT_LEN) {
			/* Boot-style: [0xA1][8-byte report] */
			kbd_report_process(&kbd, &d[1], KBD_REPORT_LEN,
					   key_event, NULL);
		} else if (len - 2 == KBD_REPORT_LEN) {
			/* Report protocol: [0xA1][report id][8-byte report] */
			kbd_report_process(&kbd, &d[2], KBD_REPORT_LEN,
					   key_event, NULL);
		}
	}
	return 0;
}

static int ctrl_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	LOG_HEXDUMP_DBG(buf->data, buf->len, "BR-HID CTRL");
	return 0;
}

/* --- channel lifecycle ----------------------------------------------------- */

static void intr_connected(struct bt_l2cap_chan *chan)
{
	LOG_INF("HID interrupt channel up -- input reports live");
}

static void intr_disconnected(struct bt_l2cap_chan *chan)
{
	LOG_INF("HID interrupt channel down");
	kbd_report_release_all(&kbd, key_event, NULL);
}

static void ctrl_connected(struct bt_l2cap_chan *chan);
static void ctrl_disconnected(struct bt_l2cap_chan *chan)
{
	LOG_INF("HID control channel down");
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

	LOG_INF("HID control channel up; opening interrupt channel");

	intr_chan.chan.ops = &intr_ops;
	intr_chan.rx.mtu = 64;

	err = bt_l2cap_chan_connect(hid_conn, &intr_chan.chan, PSM_HID_INTR);
	if (err) {
		LOG_ERR("interrupt connect failed (%d)", err);
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
		LOG_INF("inbound reconnect (PSM 0x%04x)", server->psm);
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

/* Canonical HID topology is host = central. Accepting inbound as peripheral
 * left the pad opening no channels AND ignoring our CONNECT_REQs (HW,
 * 2026-07-08); the role switch at accept fixed device-initiated reconnect.
 */
static enum bt_br_conn_req_rsp br_conn_req(const bt_addr_t *addr, uint32_t cod)
{
	char str[BT_ADDR_STR_LEN];

	bt_addr_to_str(addr, str, sizeof(str));
	LOG_INF("inbound BR connection from %s (CoD 0x%06x) -- accept as central",
		str, cod);
	return BT_BR_CONN_REQ_ACCEPT_CENTRAL;
}

/* --- public entry points ---------------------------------------------------- */

int btinput_init(void)
{
	int err;

	err = bt_l2cap_br_server_register(&ctrl_server);
	err |= bt_l2cap_br_server_register(&intr_server);
	if (err) {
		LOG_ERR("HID L2CAP server register failed");
		return -EIO;
	}

	err = bt_br_set_connectable(true, br_conn_req);
	if (err && err != -EALREADY) {
		LOG_ERR("bt_br_set_connectable failed (%d)", err);
		return err;
	}

	LOG_INF("Classic HID host up: PSM 0x11/0x13 servers, connectable (central)");
	return 0;
}

void btinput_hid_start(struct bt_conn *conn)
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
		LOG_ERR("control connect failed (%d)", err);
		return;
	}
	LOG_INF("connecting HID control channel (PSM 0x11)...");
}

void btinput_hid_stop(void)
{
	kbd_report_release_all(&kbd, key_event, NULL);
	hid_conn = NULL;
}

bool btinput_hid_active(void)
{
	return hid_conn != NULL;
}
