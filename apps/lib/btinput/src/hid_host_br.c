/*
 * apps/lib/btinput -- minimal Classic BT (BR/EDR) HID host, N devices.
 *
 * Zephyr ships BR/EDR ACL + SSP + L2CAP but no HID host profile; this is the
 * missing piece: per-device L2CAP PSM 0x11 (control) + 0x13 (interrupt)
 * channels, boot keyboard + mouse report parsing, device-initiated
 * reconnection. The lib registers its OWN connection callbacks and owns the
 * whole BR HID lifecycle -- claim a device slot on connect, elevate security,
 * open/accept the channels, tear down on disconnect -- so a consumer app gets
 * everything from btinput_init() + the event sinks alone. (BR/EDR connections
 * in a btinput app belong to this lib; LE is untouched.)
 *
 * Input demux is by HIDP boot-protocol report id (1 = keyboard, 2 = mouse),
 * falling back to payload length for id-less devices -- more robust than
 * classifying by Class-of-Device, and combo devices work for free. A
 * SET_PROTOCOL(Boot) is requested on every control channel so devices that
 * honor it emit predictable boot reports; a handshake NAK is logged and
 * tolerated (the 8BitDo streams boot-compatible id-1 frames either way).
 *
 * Hard-earned rules honored here (memory reference_zephyr_classic_hid_host):
 * accept inbound as CENTRAL; stay passive on inbound ACLs (per-slot 750 ms
 * fallback); bt_conn_get_dst() is NULL for BR (use bt_conn_get_info); never
 * memset a bt_l2cap_br_chan the stack may hold (its armed rtx_work corrupts
 * the kernel timeout queue -- claim-time only, and accept() rejects opens
 * that would race an in-flight host open).
 */

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/classic/classic.h>

#include "btinput.h"
#include "btinput_priv.h"
#include "kbd_report.h"
#include "mouse_report.h"

LOG_MODULE_REGISTER(btinput, CONFIG_BTINPUT_LOG_LEVEL);

/* HID profile PSMs (Bluetooth HID v1.1). */
#define PSM_HID_CTRL 0x0011
#define PSM_HID_INTR 0x0013

/* HID transaction header: DATA | Input report. */
#define HID_HDR_DATA_INPUT 0xa1

/* HIDP boot-protocol report ids (HID v1.1 §7.4.3). */
#define HID_REPORT_ID_KBD   0x01
#define HID_REPORT_ID_MOUSE 0x02

/* HIDP SET_PROTOCOL request: type 0x7 << 4 | param (0 = Boot). The reply is
 * a HANDSHAKE: type 0x0 << 4 | result (0 = successful).
 */
#define HIDP_SET_PROTOCOL_BOOT 0x70
#define HIDP_TRANS_MASK        0xf0
#define HIDP_TRANS_HANDSHAKE   0x00

/* CoD peripheral-minor flags (byte 0), logged at connect. */
#define COD_PERIPH_KEYBOARD 0x40
#define COD_PERIPH_POINTING 0x80

struct btinput_dev {
	struct bt_conn *conn;   /* ref held from claim to disconnect */
	struct bt_l2cap_br_chan ctrl_chan;
	struct bt_l2cap_br_chan intr_chan;
	bool inbound;           /* device paged us (vs consumer-created) */
	bool host_opened;       /* we drive the channel opens on this dev */
	bool ctrl_up;
	bool intr_up;
	struct k_work_delayable fallback_work;
	struct kbd_state kbd;
	struct mouse_state mouse;
};

static struct btinput_dev devs[CONFIG_BTINPUT_MAX_DEVICES];

/* CoD arrives only in the connection-request callback; park it (keyed by
 * address) until connected() can claim a slot for the same peer.
 */
static struct {
	bt_addr_t addr;
	uint32_t cod;
	bool used;
} pending_inbound[CONFIG_BTINPUT_MAX_DEVICES];

/* One tiny TX buf per device covers the single-byte SET_PROTOCOL request. */
NET_BUF_POOL_FIXED_DEFINE(btinput_tx_pool, CONFIG_BTINPUT_MAX_DEVICES,
			  BT_L2CAP_BUF_SIZE(2), CONFIG_BT_CONN_TX_USER_DATA_SIZE,
			  NULL);

/* --- consumer sinks -------------------------------------------------------- */

static btinput_key_cb_t s_key_cb;
static void *s_key_user;
static btinput_mouse_btn_cb_t s_mbtn_cb;
static btinput_mouse_move_cb_t s_mmove_cb;
static void *s_mouse_user;

void btinput_set_key_cb(btinput_key_cb_t cb, void *user)
{
	s_key_cb = cb;
	s_key_user = user;
}

void btinput_set_mouse_cbs(btinput_mouse_btn_cb_t btn_cb,
			   btinput_mouse_move_cb_t move_cb, void *user)
{
	s_mbtn_cb = btn_cb;
	s_mmove_cb = move_cb;
	s_mouse_user = user;
}

/* Parser trampolines -> the consumer sinks. Shared with the LE backend
 * (btinput_priv.h), so both feed the same registered sinks.
 */
void btinput_key_trampoline(uint8_t usage, bool pressed, void *user_data)
{
	ARG_UNUSED(user_data);

	if (s_key_cb != NULL) {
		s_key_cb(usage, pressed, s_key_user);
	}
}

void btinput_mouse_btn_trampoline(uint8_t button, bool pressed, void *user_data)
{
	ARG_UNUSED(user_data);

	if (s_mbtn_cb != NULL) {
		s_mbtn_cb(button, pressed, s_mouse_user);
	}
}

void btinput_mouse_move_trampoline(int8_t dx, int8_t dy, int8_t wheel,
				   void *user_data)
{
	ARG_UNUSED(user_data);

	if (s_mmove_cb != NULL) {
		s_mmove_cb(dx, dy, wheel, s_mouse_user);
	}
}

/* --- slot bookkeeping ------------------------------------------------------- */

static struct btinput_dev *dev_find(struct bt_conn *conn)
{
	for (int i = 0; i < ARRAY_SIZE(devs); i++) {
		if (devs[i].conn == conn) {
			return &devs[i];
		}
	}
	return NULL;
}

static const bt_addr_t *conn_br_dst(struct bt_conn *conn)
{
	static struct bt_conn_info info; /* BT RX ctx only; not reentrant */

	if (bt_conn_get_info(conn, &info) != 0 ||
	    info.type != BT_CONN_TYPE_BR) {
		return NULL;
	}
	return info.br.dst;
}

static void dev_release_input(struct btinput_dev *dev)
{
	kbd_report_release_all(&dev->kbd, btinput_key_trampoline, NULL);
	mouse_report_release_all(&dev->mouse, btinput_mouse_btn_trampoline, NULL);
}

/* --- interrupt channel: input reports --------------------------------------- */

static struct btinput_dev *dev_from_intr(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_br_chan *br =
		CONTAINER_OF(chan, struct bt_l2cap_br_chan, chan);

	return CONTAINER_OF(br, struct btinput_dev, intr_chan);
}

static struct btinput_dev *dev_from_ctrl(struct bt_l2cap_chan *chan)
{
	struct bt_l2cap_br_chan *br =
		CONTAINER_OF(chan, struct bt_l2cap_br_chan, chan);

	return CONTAINER_OF(br, struct btinput_dev, ctrl_chan);
}

/* Demux one DATA|Input payload into the right parser. Boot-protocol report
 * ids are authoritative (kbd 1 / mouse 2); id-less frames fall back to the
 * only lengths that make sense (8 = boot keyboard, 3..4 = boot mouse).
 */
static void input_demux(struct btinput_dev *dev, const uint8_t *d, uint16_t len)
{
	if (len >= 2) {
		uint8_t id = d[0];
		const uint8_t *p = &d[1];
		int plen = len - 1;

		if (id == HID_REPORT_ID_KBD && plen >= KBD_REPORT_LEN) {
			kbd_report_process(&dev->kbd, p, plen, btinput_key_trampoline, NULL);
			return;
		}
		if (id == HID_REPORT_ID_MOUSE && plen >= MOUSE_REPORT_MIN_LEN) {
			mouse_report_process(&dev->mouse, p, MIN(plen, 4),
					     btinput_mouse_btn_trampoline, btinput_mouse_move_trampoline,
					     NULL);
			return;
		}
	}

	if (len == KBD_REPORT_LEN) {
		kbd_report_process(&dev->kbd, d, len, btinput_key_trampoline, NULL);
	} else if (len >= MOUSE_REPORT_MIN_LEN && len <= 4) {
		mouse_report_process(&dev->mouse, d, len, btinput_mouse_btn_trampoline,
				     btinput_mouse_move_trampoline, NULL);
	} else {
		LOG_HEXDUMP_DBG(d, len, "unhandled input report");
	}
}

static int intr_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	struct btinput_dev *dev = dev_from_intr(chan);

	LOG_HEXDUMP_DBG(buf->data, buf->len, "BR-HID INTR");

	if (buf->len >= 2 && buf->data[0] == HID_HDR_DATA_INPUT) {
		input_demux(dev, &buf->data[1], buf->len - 1);
	}
	return 0;
}

/* --- control channel: SET_PROTOCOL + handshakes ------------------------------ */

static void set_protocol_boot(struct btinput_dev *dev)
{
	struct net_buf *buf;
	int err;

	buf = net_buf_alloc(&btinput_tx_pool, K_NO_WAIT);
	if (buf == NULL) {
		LOG_WRN("no buf for SET_PROTOCOL; staying in report protocol");
		return;
	}

	net_buf_reserve(buf, BT_L2CAP_CHAN_SEND_RESERVE);
	net_buf_add_u8(buf, HIDP_SET_PROTOCOL_BOOT);

	err = bt_l2cap_chan_send(&dev->ctrl_chan.chan, buf);
	if (err < 0) {
		net_buf_unref(buf);
		LOG_WRN("SET_PROTOCOL send failed (%d)", err);
	}
}

static int ctrl_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	if (buf->len == 1 &&
	    (buf->data[0] & HIDP_TRANS_MASK) == HIDP_TRANS_HANDSHAKE) {
		/* Reply to our SET_PROTOCOL. 0 = boot protocol accepted;
		 * anything else = device keeps report protocol (fine, the
		 * demux handles both).
		 */
		LOG_INF("HID handshake 0x%02x (%s)", buf->data[0],
			buf->data[0] == 0 ? "boot protocol" : "kept report protocol");
		return 0;
	}

	LOG_HEXDUMP_DBG(buf->data, buf->len, "BR-HID CTRL");
	return 0;
}

/* --- channel lifecycle -------------------------------------------------------- */

static const struct bt_l2cap_chan_ops ctrl_ops;
static const struct bt_l2cap_chan_ops intr_ops;

static void open_intr(struct btinput_dev *dev)
{
	int err;

	/* Safe: intr_chan was memset at claim and, with host_opened set,
	 * accept() rejects any competing device-side open of this PSM.
	 */
	dev->intr_chan.chan.ops = &intr_ops;
	dev->intr_chan.rx.mtu = 64;

	err = bt_l2cap_chan_connect(dev->conn, &dev->intr_chan.chan,
				    PSM_HID_INTR);
	if (err) {
		LOG_ERR("interrupt connect failed (%d)", err);
	}
}

static void ctrl_connected(struct bt_l2cap_chan *chan)
{
	struct btinput_dev *dev = dev_from_ctrl(chan);

	dev->ctrl_up = true;
	LOG_INF("HID control channel up (%s)",
		dev->host_opened ? "host-opened" : "device-opened");

	set_protocol_boot(dev);

	/* Outbound path: we drive both opens. Inbound: stay passive, the
	 * device opens interrupt itself (fallback_work covers stragglers).
	 */
	if (dev->host_opened) {
		open_intr(dev);
	}
}

static void ctrl_disconnected(struct bt_l2cap_chan *chan)
{
	struct btinput_dev *dev = dev_from_ctrl(chan);

	dev->ctrl_up = false;
	LOG_INF("HID control channel down");
}

static void intr_connected(struct bt_l2cap_chan *chan)
{
	struct btinput_dev *dev = dev_from_intr(chan);

	dev->intr_up = true;
	LOG_INF("HID interrupt channel up -- input reports live");
}

static void intr_disconnected(struct bt_l2cap_chan *chan)
{
	struct btinput_dev *dev = dev_from_intr(chan);

	dev->intr_up = false;
	LOG_INF("HID interrupt channel down");
	dev_release_input(dev);
}

static const struct bt_l2cap_chan_ops ctrl_ops = {
	.recv = ctrl_recv,
	.connected = ctrl_connected,
	.disconnected = ctrl_disconnected,
};

static const struct bt_l2cap_chan_ops intr_ops = {
	.recv = intr_recv,
	.connected = intr_connected,
	.disconnected = intr_disconnected,
};

/* Inbound ACL secured but the device opened no (or only one) channel within
 * the grace period: host-initiate the rest. Some devices page in but expect
 * host-opened channels.
 */
static void fallback_fn(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct btinput_dev *dev =
		CONTAINER_OF(dwork, struct btinput_dev, fallback_work);

	if (dev->conn == NULL || dev->intr_up) {
		return;
	}

	dev->host_opened = true; /* accept() now rejects competing opens */

	if (!dev->ctrl_up) {
		LOG_INF("device opened no channels; host-initiating");
		dev->ctrl_chan.chan.ops = &ctrl_ops;
		dev->ctrl_chan.rx.mtu = 64;
		if (bt_l2cap_chan_connect(dev->conn, &dev->ctrl_chan.chan,
					  PSM_HID_CTRL)) {
			LOG_ERR("control connect failed");
		}
	} else {
		LOG_INF("device opened control only; host-opening interrupt");
		open_intr(dev);
	}
}

/* --- inbound path: servers + connection requests ------------------------------ */

static int hid_server_accept(struct bt_conn *conn,
			     struct bt_l2cap_server *server,
			     struct bt_l2cap_chan **chan)
{
	struct btinput_dev *dev = dev_find(conn);
	struct bt_l2cap_br_chan *c;
	bool up;

	if (dev == NULL) {
		/* Unknown conn (no free slot at connect, or not BR HID). */
		return -ENOMEM;
	}

	/* We are mid host-open on this device: a competing device-side open
	 * would hand the stack the SAME chan object our connect already owns
	 * (its armed rtx_work memset = kernel timeout corruption -- the
	 * 2026-07-08 crash). Reject; our open is in flight.
	 */
	if (dev->host_opened) {
		return -EACCES;
	}

	if (server->psm == PSM_HID_CTRL) {
		c = &dev->ctrl_chan;
		up = dev->ctrl_up;
	} else {
		c = &dev->intr_chan;
		up = dev->intr_up;
	}

	/* Re-open of a live channel: same rule, never reset it under the
	 * stack. The device must disconnect it first.
	 */
	if (up) {
		return -EACCES;
	}

	LOG_INF("device opens PSM 0x%04x", server->psm);

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

/* Canonical HID topology is host = central: accepting as peripheral left the
 * 8BitDo opening no channels AND ignoring host CONNECT_REQs (HW 2026-07-08).
 */
static enum bt_br_conn_req_rsp br_conn_req(const bt_addr_t *addr, uint32_t cod)
{
	char str[BT_ADDR_STR_LEN];

	/* One entry per address: a request whose ACL setup failed leaves a
	 * stale entry no connected() callback ever consumes -- refresh it
	 * rather than leaking pool slots.
	 */
	int slot = -1;

	for (int i = 0; i < ARRAY_SIZE(pending_inbound); i++) {
		if (pending_inbound[i].used &&
		    bt_addr_eq(&pending_inbound[i].addr, addr)) {
			slot = i;
			break;
		}
		if (slot < 0 && !pending_inbound[i].used) {
			slot = i;
		}
	}
	if (slot >= 0) {
		pending_inbound[slot].addr = *addr;
		pending_inbound[slot].cod = cod;
		pending_inbound[slot].used = true;
	}

	bt_addr_to_str(addr, str, sizeof(str));
	LOG_INF("inbound BR connection from %s (CoD 0x%06x%s%s) -- accept as central",
		str, cod,
		(cod & COD_PERIPH_KEYBOARD) ? " kbd" : "",
		(cod & COD_PERIPH_POINTING) ? " ptr" : "");
	return BT_BR_CONN_REQ_ACCEPT_CENTRAL;
}

/* Matches (and consumes) a pending inbound entry for this address. */
static bool inbound_pending_take(const bt_addr_t *addr, uint32_t *cod)
{
	for (int i = 0; i < ARRAY_SIZE(pending_inbound); i++) {
		if (pending_inbound[i].used &&
		    bt_addr_eq(&pending_inbound[i].addr, addr)) {
			*cod = pending_inbound[i].cod;
			pending_inbound[i].used = false;
			return true;
		}
	}
	return false;
}

/* --- connection lifecycle (lib-owned for BR) ----------------------------------- */

static void bti_connected(struct bt_conn *conn, uint8_t err)
{
	const bt_addr_t *addr = conn_br_dst(conn);
	struct btinput_dev *dev;
	char str[BT_ADDR_STR_LEN];
	uint32_t cod = 0;
	bool inbound;

	if (addr == NULL) {
		return; /* LE or no info: not ours */
	}

	if (err) {
		/* Failed connect: do NOT touch the pending-inbound stash.
		 * A failed OUTBOUND page to a device that is concurrently
		 * paging us shares the address -- consuming its entry here
		 * misclassified the subsequent inbound as outbound (host
		 * opened channels into a device-initiated link; HW
		 * 2026-07-08). A stale inbound entry, if any, is overwritten
		 * by the device's next conn_req.
		 */
		return;
	}

	inbound = inbound_pending_take(addr, &cod);

	if (dev_find(conn) != NULL) {
		return;
	}

	dev = dev_find(NULL); /* free slot */
	if (dev == NULL) {
		LOG_WRN("no free HID device slot (max %d)",
			CONFIG_BTINPUT_MAX_DEVICES);
		return;
	}

	/* Claim. Safe to reset the channel objects here: a freed slot's
	 * channels were fully torn down before its disconnect callback ran.
	 */
	memset(&dev->ctrl_chan, 0, sizeof(dev->ctrl_chan));
	memset(&dev->intr_chan, 0, sizeof(dev->intr_chan));
	memset(&dev->kbd, 0, sizeof(dev->kbd));
	memset(&dev->mouse, 0, sizeof(dev->mouse));
	dev->inbound = inbound;
	dev->host_opened = false;
	dev->ctrl_up = false;
	dev->intr_up = false;
	dev->conn = bt_conn_ref(conn);

	bt_addr_to_str(addr, str, sizeof(str));
	LOG_INF("HID slot %d <- %s (%s)", (int)(dev - devs), str,
		inbound ? "inbound" : "outbound");

	/* HID channels demand L2; drive it from the lib so consumers need
	 * no connection callbacks of their own.
	 */
	if (bt_conn_set_security(conn, BT_SECURITY_L2) < 0) {
		LOG_WRN("set_security failed; waiting for peer/consumer");
	}
}

static void bti_security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err)
{
	struct btinput_dev *dev = dev_find(conn);

	if (dev == NULL) {
		return;
	}

	if (err) {
		/* Retry/rescue policy belongs to the consumer; disconnect
		 * cleanup will free the slot.
		 */
		LOG_DBG("security failed (%d)", err);
		return;
	}

	if (dev->inbound) {
		/* Stay passive: the device opens PSM 0x11 + 0x13 itself.
		 * Host-opening too collides in L2CAP signaling.
		 */
		LOG_INF("secured (inbound) -- awaiting device channel opens");
		k_work_schedule(&dev->fallback_work, K_MSEC(750));
	} else {
		/* Outbound (consumer-paged): we drive both opens,
		 * control first (HID v1.1 order).
		 */
		dev->host_opened = true;
		dev->ctrl_chan.chan.ops = &ctrl_ops;
		dev->ctrl_chan.rx.mtu = 64;
		if (bt_l2cap_chan_connect(conn, &dev->ctrl_chan.chan,
					  PSM_HID_CTRL)) {
			LOG_ERR("control connect failed");
		} else {
			LOG_INF("connecting HID control channel (PSM 0x11)...");
		}
	}
}

static void bti_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct btinput_dev *dev = dev_find(conn);

	if (dev == NULL) {
		return;
	}

	k_work_cancel_delayable(&dev->fallback_work);
	dev_release_input(dev);
	dev->ctrl_up = false;
	dev->intr_up = false;
	/* Channel objects are NOT reset here (the stack may still reference
	 * them during teardown); the next claim resets them.
	 */
	bt_conn_unref(dev->conn);
	dev->conn = NULL;
	LOG_INF("HID slot %d freed (reason 0x%02x)", (int)(dev - devs), reason);
}

BT_CONN_CB_DEFINE(btinput_conn_cb) = {
	.connected = bti_connected,
	.disconnected = bti_disconnected,
	.security_changed = bti_security_changed,
};

/* --- public entry points -------------------------------------------------------- */

int btinput_init(void)
{
	static bool done;
	int err;

	if (done) {
		return 0;
	}

	for (int i = 0; i < ARRAY_SIZE(devs); i++) {
		k_work_init_delayable(&devs[i].fallback_work, fallback_fn);
	}

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

	done = true;
	LOG_INF("Classic HID host up: PSM 0x11/0x13 servers, connectable "
		"(central), %d device slots", CONFIG_BTINPUT_MAX_DEVICES);
	return 0;
}

int btinput_pair_mode(bool enable)
{
	int err = bt_br_set_discoverable(enable, false);

	if (err && err != -EALREADY) {
		LOG_ERR("set_discoverable(%d) failed (%d)", enable, err);
		return err;
	}
	LOG_INF("pair mode %s", enable ? "ON (discoverable)" : "off");
	return 0;
}
