/*
 * apps/lib/btinput -- LE HOGP (HID over GATT) backend.
 *
 * BLE-only HID devices (Logitech MX-series mice, most modern BLE keyboards)
 * never appear on BR/EDR -- they expose HIDS (0x1812) over GATT. This backend
 * turns a secured LE connection into btinput events: discover HIDS, prefer
 * BOOT protocol (write Protocol Mode = 0, subscribe the Boot Mouse/Keyboard
 * Input Report characteristics -- fixed formats our parsers already speak),
 * fall back to subscribing report-protocol input reports with a length
 * heuristic (8 = keyboard, 3..4 = mouse) and hexdumps for anything exotic.
 *
 * Unlike the BR side (device-initiated, lib-owned lifecycle), LE connections
 * are CONSUMER-initiated: scanning/connect/security policy is very
 * app-specific, so the consumer calls btinput_le_attach() on a connected +
 * secured LE conn and the lib takes it from there (detaching itself if the
 * peer has no HIDS). Teardown on disconnect is automatic.
 *
 * Hard-earned LE rules honored (memories zephyr-gatt-discover-uuid-static,
 * project_wo_bt_k1_m0_bringup): discovery UUIDs live in per-slot static
 * storage; subscriptions are VOLATILE with auto-CCC bounded to value+2;
 * state is reset at attach only, never on the disconnect path.
 */

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "btinput.h"
#include "btinput_priv.h"
#include "kbd_report.h"
#include "mouse_report.h"

#ifdef CONFIG_BTINPUT_HOGP

LOG_MODULE_REGISTER(btinput_le, CONFIG_BTINPUT_LOG_LEVEL);

#define LE_MAX_SUBS 6

enum le_sub_kind {
	KIND_BOOT_KBD,
	KIND_BOOT_MOUSE,
	KIND_REPORT, /* report-protocol input report; length heuristic */
};

struct le_sub {
	struct btinput_le_dev *dev;
	uint16_t value_handle;
	uint8_t kind;
	bool used;
	struct bt_gatt_subscribe_params sub;
	struct bt_gatt_discover_params ccc_disc;
};

struct btinput_le_dev {
	struct bt_conn *conn;
	uint16_t hids_start;
	uint16_t hids_end;
	uint16_t proto_mode_vh;
	bool have_boot;
	bool armed;
	struct le_sub subs[LE_MAX_SUBS];
	int n_subs;
	struct bt_gatt_discover_params disc;
	struct bt_uuid_16 disc_uuid; /* static storage: the stack keeps the ptr */
	struct kbd_state kbd;
	struct mouse_state mouse;
};

static struct btinput_le_dev le_devs[CONFIG_BTINPUT_MAX_DEVICES];

static struct btinput_le_dev *le_find(struct bt_conn *conn)
{
	for (int i = 0; i < ARRAY_SIZE(le_devs); i++) {
		if (le_devs[i].conn == conn) {
			return &le_devs[i];
		}
	}
	return NULL;
}

static void le_release_input(struct btinput_le_dev *dev)
{
	kbd_report_release_all(&dev->kbd, btinput_key_trampoline, NULL);
	mouse_report_release_all(&dev->mouse, btinput_mouse_btn_trampoline, NULL);
}

/* Detach without touching subscription params: they are VOLATILE, so the
 * host drops them with the connection (wiping them here is the PC=0 class).
 */
static void le_detach(struct btinput_le_dev *dev)
{
	le_release_input(dev);
	bt_conn_unref(dev->conn);
	dev->conn = NULL;
	LOG_INF("LE slot %d detached", (int)(dev - le_devs));
}

/* --- notifications ---------------------------------------------------------- */

static uint8_t le_notify_cb(struct bt_conn *conn,
			    struct bt_gatt_subscribe_params *params,
			    const void *data, uint16_t length)
{
	struct le_sub *s = CONTAINER_OF(params, struct le_sub, sub);
	struct btinput_le_dev *dev = s->dev;
	const uint8_t *d = data;

	if (data == NULL) {
		return BT_GATT_ITER_STOP; /* unsubscribed */
	}

	switch (s->kind) {
	case KIND_BOOT_KBD:
		kbd_report_process(&dev->kbd, d, length,
				   btinput_key_trampoline, NULL);
		break;
	case KIND_BOOT_MOUSE:
		mouse_report_process(&dev->mouse, d, MIN(length, 4),
				     btinput_mouse_btn_trampoline,
				     btinput_mouse_move_trampoline, NULL);
		break;
	case KIND_REPORT:
		/* Report protocol: no report id in the notification (it lives
		 * in the 0x2908 descriptor). Boot-shaped lengths get parsed;
		 * everything else is dumped so the format can be learned.
		 */
		if (length == KBD_REPORT_LEN) {
			kbd_report_process(&dev->kbd, d, length,
					   btinput_key_trampoline, NULL);
		} else if (length >= MOUSE_REPORT_MIN_LEN && length <= 4) {
			mouse_report_process(&dev->mouse, d, length,
					     btinput_mouse_btn_trampoline,
					     btinput_mouse_move_trampoline,
					     NULL);
		} else {
			LOG_HEXDUMP_INF(d, length, "report (unknown format)");
		}
		break;
	default:
		break;
	}
	return BT_GATT_ITER_CONTINUE;
}

/* --- configuration after discovery ------------------------------------------ */

static void le_subscribe_one(struct btinput_le_dev *dev, struct le_sub *s)
{
	int err;

	s->sub.notify = le_notify_cb;
	s->sub.value = BT_GATT_CCC_NOTIFY;
	s->sub.value_handle = s->value_handle;
	s->sub.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
	/* CCC sits right after the value handle; bound the search tightly. */
	s->sub.end_handle = s->value_handle + 2;
	s->sub.disc_params = &s->ccc_disc;
	/* Host drops these on disconnect -- never a stale auto-resubscribe. */
	atomic_set_bit(s->sub.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(dev->conn, &s->sub);
	if (err && err != -EALREADY) {
		LOG_WRN("subscribe 0x%04x failed (%d)", s->value_handle, err);
	} else {
		LOG_INF("subscribed 0x%04x (%s)", s->value_handle,
			s->kind == KIND_BOOT_KBD    ? "boot kbd"
			: s->kind == KIND_BOOT_MOUSE ? "boot mouse"
						     : "report");
	}
}

static void le_configure(struct btinput_le_dev *dev)
{
	/* Prefer boot protocol: fixed report formats, no Report Map parsing.
	 * Per HOGP a device exposing the boot characteristics honors
	 * Protocol Mode writes (write-without-response).
	 */
	if (dev->have_boot && dev->proto_mode_vh != 0) {
		static const uint8_t boot = 0x00;
		int err = bt_gatt_write_without_response(dev->conn,
							 dev->proto_mode_vh,
							 &boot, sizeof(boot),
							 false);

		LOG_INF("Protocol Mode <- Boot (%d)", err);
	}

	for (int i = 0; i < dev->n_subs; i++) {
		struct le_sub *s = &dev->subs[i];

		/* With boot inputs present, the boot chars are the data path;
		 * report-protocol chars stay unsubscribed (double-parse).
		 */
		if (dev->have_boot && s->kind == KIND_REPORT) {
			continue;
		}
		le_subscribe_one(dev, s);
	}

	if (!dev->have_boot) {
		LOG_INF("no boot input chars -- report protocol with length "
			"heuristic (8=kbd, 3-4=mouse; others hexdumped)");
	}
	dev->armed = true;
	LOG_INF("HOGP input armed");
}

bool btinput_le_armed(struct bt_conn *conn)
{
	struct btinput_le_dev *dev = le_find(conn);

	return dev != NULL && dev->armed;
}

/* --- discovery --------------------------------------------------------------- */

static void le_add_sub(struct btinput_le_dev *dev, uint16_t vh, uint8_t kind)
{
	if (dev->n_subs >= LE_MAX_SUBS) {
		LOG_WRN("sub table full; dropping 0x%04x", vh);
		return;
	}

	struct le_sub *s = &dev->subs[dev->n_subs++];

	s->dev = dev;
	s->value_handle = vh;
	s->kind = kind;
	s->used = true;
}

static uint8_t le_char_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  struct bt_gatt_discover_params *params)
{
	struct btinput_le_dev *dev = CONTAINER_OF(params, struct btinput_le_dev,
						  disc);

	if (attr == NULL) {
		LOG_INF("HIDS: %d input char(s)%s%s", dev->n_subs,
			dev->have_boot ? ", boot capable" : "",
			dev->proto_mode_vh ? ", protocol mode" : "");
		if (dev->n_subs == 0) {
			LOG_WRN("HIDS has no notifiable input chars -- detaching");
			le_detach(dev);
		} else {
			le_configure(dev);
		}
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_chrc *chrc = attr->user_data;

	if (bt_uuid_cmp(chrc->uuid, BT_UUID_HIDS_PROTOCOL_MODE) == 0) {
		dev->proto_mode_vh = chrc->value_handle;
	} else if (bt_uuid_cmp(chrc->uuid, BT_UUID_HIDS_BOOT_KB_IN_REPORT) == 0 &&
		   (chrc->properties & BT_GATT_CHRC_NOTIFY)) {
		dev->have_boot = true;
		le_add_sub(dev, chrc->value_handle, KIND_BOOT_KBD);
	} else if (bt_uuid_cmp(chrc->uuid, BT_UUID_HIDS_BOOT_MOUSE_IN_REPORT) == 0 &&
		   (chrc->properties & BT_GATT_CHRC_NOTIFY)) {
		dev->have_boot = true;
		le_add_sub(dev, chrc->value_handle, KIND_BOOT_MOUSE);
	} else if (bt_uuid_cmp(chrc->uuid, BT_UUID_HIDS_REPORT) == 0 &&
		   (chrc->properties & BT_GATT_CHRC_NOTIFY)) {
		/* Notify-capable Report = an input report. */
		le_add_sub(dev, chrc->value_handle, KIND_REPORT);
	}
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t le_svc_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 struct bt_gatt_discover_params *params)
{
	struct btinput_le_dev *dev = CONTAINER_OF(params, struct btinput_le_dev,
						  disc);

	if (attr == NULL) {
		if (dev->hids_start == 0) {
			LOG_INF("peer has no HIDS -- detaching (not a HID device)");
			le_detach(dev);
			return BT_GATT_ITER_STOP;
		}

		/* Walk the HIDS characteristics. */
		dev->disc.uuid = NULL;
		dev->disc.func = le_char_cb;
		dev->disc.start_handle = dev->hids_start;
		dev->disc.end_handle = dev->hids_end;
		dev->disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		if (bt_gatt_discover(dev->conn, &dev->disc) != 0) {
			LOG_WRN("char discovery failed -- detaching");
			le_detach(dev);
		}
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_service_val *svc = attr->user_data;

	if (dev->hids_start == 0) {
		dev->hids_start = attr->handle + 1;
		dev->hids_end = svc->end_handle;
		LOG_INF("HIDS at 0x%04x..0x%04x", attr->handle, svc->end_handle);
	} else {
		LOG_WRN("second HIDS instance at 0x%04x ignored", attr->handle);
	}
	return BT_GATT_ITER_CONTINUE;
}

/* --- lifecycle ---------------------------------------------------------------- */

int btinput_le_attach(struct bt_conn *conn)
{
	struct btinput_le_dev *dev;

	if (le_find(conn) != NULL) {
		return -EALREADY;
	}

	dev = le_find(NULL);
	if (dev == NULL) {
		LOG_WRN("no free LE HID slot (max %d)", CONFIG_BTINPUT_MAX_DEVICES);
		return -ENOMEM;
	}

	/* Reset is safe here: the slot's previous connection is gone and its
	 * subscriptions were volatile (host already dropped them).
	 */
	memset(dev, 0, sizeof(*dev));
	dev->conn = bt_conn_ref(conn);

	dev->disc_uuid = (struct bt_uuid_16)BT_UUID_INIT_16(BT_UUID_HIDS_VAL);
	dev->disc.uuid = &dev->disc_uuid.uuid;
	dev->disc.func = le_svc_cb;
	dev->disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	dev->disc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	dev->disc.type = BT_GATT_DISCOVER_PRIMARY;

	if (bt_gatt_discover(conn, &dev->disc) != 0) {
		LOG_WRN("HIDS discovery start failed");
		le_detach(dev);
		return -EIO;
	}

	LOG_INF("LE slot %d: discovering HIDS...", (int)(dev - le_devs));
	return 0;
}

static void le_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct btinput_le_dev *dev = le_find(conn);

	if (dev == NULL) {
		return;
	}

	/* Subscription params are volatile: the host already dropped them.
	 * Release held input and free the slot; the next attach resets it.
	 */
	le_release_input(dev);
	bt_conn_unref(dev->conn);
	dev->conn = NULL;
	LOG_INF("LE slot %d freed (reason 0x%02x)", (int)(dev - le_devs), reason);
}

BT_CONN_CB_DEFINE(btinput_le_conn_cb) = {
	.disconnected = le_disconnected,
};

#else /* !CONFIG_BTINPUT_HOGP */

int btinput_le_attach(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	return -ENOTSUP;
}

bool btinput_le_armed(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	return false;
}

#endif /* CONFIG_BTINPUT_HOGP */
