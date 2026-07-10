/*
 * WO-BT-K1 M3 — GATT sniffer / input-path discovery.
 *
 * Empirical finding (HW 2026-07-08): the 8BitDo Micro in K mode exposes NO
 * HOGP (0x1812) over BLE -- only GAP, GATT and a vendor service 0xFF10. Its
 * USB keyboard is a separate HID path. So the key data must flow over a
 * vendor characteristic, not HOGP. Rather than assume the format, this
 * sniffs it: enumerate every characteristic, dump readable values, subscribe
 * to every notify/indicate characteristic in the whole DB, and hexdump
 * whatever arrives on a keypress. 8-byte payloads are also run through the
 * boot-keyboard parser in case the vendor pipe carries standard reports.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "hogp.h"
#include "kbd_report.h"

#define MAX_CHARS 16

struct sniff_char {
	uint16_t value_handle;
	uint8_t props;
	bool is_sc; /* Service Changed (0x2a05) */
	char uuid_str[BT_UUID_STR_LEN];
	bool sub_armed;
	struct bt_gatt_subscribe_params sub;
	struct bt_gatt_discover_params ccc_disc;
};

static void reenumerate(void);

static struct {
	struct bt_conn *conn;
	struct sniff_char chars[MAX_CHARS];
	int n;
	int read_idx;
	struct bt_gatt_discover_params disc;
	struct bt_gatt_read_params read;
	struct kbd_state kbd;
} ctx;

/* --- notifications -------------------------------------------------------- */

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

static uint8_t notify_cb(struct bt_conn *conn,
			 struct bt_gatt_subscribe_params *params,
			 const void *data, uint16_t length)
{
	struct sniff_char *c = CONTAINER_OF(params, struct sniff_char, sub);

	if (data == NULL) {
		printk("NOTIFY handle 0x%04x unsubscribed\n", c->value_handle);
		c->sub_armed = false;
		return BT_GATT_ITER_STOP;
	}

	char tag[64];

	snprintk(tag, sizeof(tag), "NOTIFY 0x%04x %s", c->value_handle, c->uuid_str);
	hexdump(tag, data, length);

	/* A Service Changed indication means the peer's GATT DB just morphed --
	 * this is how the pad would reveal a HID service that wasn't there on
	 * first connect. Re-enumerate to catch it.
	 */
	if (c->is_sc) {
		printk("*** SERVICE CHANGED -- re-enumerating GATT DB ***\n");
		reenumerate();
		return BT_GATT_ITER_CONTINUE;
	}

	/* Speculatively decode as a boot keyboard report. */
	if (length == KBD_REPORT_LEN) {
		kbd_report_process(&ctx.kbd, data, length, key_event, NULL);
	}
	return BT_GATT_ITER_CONTINUE;
}

/* --- read every readable characteristic once (dump static content) -------- */

static void read_next(void);

static uint8_t read_cb(struct bt_conn *conn, uint8_t err,
		       struct bt_gatt_read_params *params,
		       const void *data, uint16_t length)
{
	if (!err && data != NULL) {
		char tag[64];

		snprintk(tag, sizeof(tag), "READ  0x%04x %s",
			 ctx.chars[ctx.read_idx].value_handle,
			 ctx.chars[ctx.read_idx].uuid_str);
		hexdump(tag, data, length);
		return BT_GATT_ITER_CONTINUE;
	}

	ctx.read_idx++;
	read_next();
	return BT_GATT_ITER_STOP;
}

static void read_next(void)
{
	while (ctx.read_idx < ctx.n &&
	       !(ctx.chars[ctx.read_idx].props & BT_GATT_CHRC_READ)) {
		ctx.read_idx++;
	}

	if (ctx.read_idx >= ctx.n) {
		printk("HOGP/sniff: armed. Press EVERY button -- watch for "
		       "NOTIFY lines (that is the keyboard data path).\n");
		return;
	}

	ctx.read.func = read_cb;
	ctx.read.handle_count = 1;
	ctx.read.single.handle = ctx.chars[ctx.read_idx].value_handle;
	ctx.read.single.offset = 0;

	if (bt_gatt_read(ctx.conn, &ctx.read) != 0) {
		ctx.read_idx++;
		read_next();
	}
}

/* --- subscribe to every notify/indicate characteristic ------------------- */

static void subscribe_all(void)
{
	for (int i = 0; i < ctx.n; i++) {
		struct sniff_char *c = &ctx.chars[i];

		if (!(c->props & (BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE))) {
			continue;
		}

		c->sub.notify = notify_cb;
		c->sub.value = (c->props & BT_GATT_CHRC_NOTIFY)
				       ? BT_GATT_CCC_NOTIFY
				       : BT_GATT_CCC_INDICATE;
		c->sub.value_handle = c->value_handle;
		c->sub.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
		/* CCC sits right after the value handle; bound the search
		 * tightly so it can't grab a neighbour char's descriptor.
		 */
		c->sub.end_handle = c->value_handle + 2;
		c->sub.disc_params = &c->ccc_disc;
		/* Auto-remove on disconnect so the host never retains these
		 * params past the connection -- otherwise hogp_start's reset
		 * would zero a live subscription and the host would later call
		 * a NULL notify fn (PC=0 crash observed 2026-07-08).
		 */
		atomic_set_bit(c->sub.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

		int err = bt_gatt_subscribe(ctx.conn, &c->sub);

		if (err && err != -EALREADY) {
			printk("  subscribe 0x%04x failed (%d)\n",
			       c->value_handle, err);
		} else {
			c->sub_armed = true;
			printk("  subscribed 0x%04x %s\n", c->value_handle,
			       c->uuid_str);
		}
	}
}

/* --- characteristic enumeration ------------------------------------------ */

static uint8_t char_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		       struct bt_gatt_discover_params *params)
{
	if (attr == NULL) {
		printk("HOGP/sniff: %d characteristics; subscribing to "
		       "notify/indicate ones\n", ctx.n);
		subscribe_all();
		ctx.read_idx = 0;
		read_next();
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_chrc *chrc = attr->user_data;

	if (ctx.n < MAX_CHARS) {
		struct sniff_char *c = &ctx.chars[ctx.n++];

		c->value_handle = chrc->value_handle;
		c->props = chrc->properties;
		c->is_sc = (bt_uuid_cmp(chrc->uuid, BT_UUID_GATT_SC) == 0);
		bt_uuid_to_str(chrc->uuid, c->uuid_str, sizeof(c->uuid_str));
		printk("  char 0x%04x %s props=0x%02x%s%s%s%s\n",
		       chrc->value_handle, c->uuid_str, chrc->properties,
		       (chrc->properties & BT_GATT_CHRC_READ) ? " RD" : "",
		       (chrc->properties & BT_GATT_CHRC_WRITE) ? " WR" : "",
		       (chrc->properties & BT_GATT_CHRC_NOTIFY) ? " NTF" : "",
		       (chrc->properties & BT_GATT_CHRC_INDICATE) ? " IND" : "");
	}
	return BT_GATT_ITER_CONTINUE;
}

/* --- primary-service enumeration (context) -------------------------------- */

static uint8_t svc_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		      struct bt_gatt_discover_params *params)
{
	if (attr == NULL) {
		/* Now walk every characteristic in the whole DB. */
		ctx.disc.uuid = NULL;
		ctx.disc.func = char_cb;
		ctx.disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		ctx.disc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		ctx.disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		if (bt_gatt_discover(ctx.conn, &ctx.disc) != 0) {
			printk("HOGP/sniff: char discovery failed\n");
		}
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_service_val *svc = attr->user_data;
	char uuid_str[BT_UUID_STR_LEN];
	bool hids = (bt_uuid_cmp(svc->uuid, BT_UUID_HIDS) == 0);

	bt_uuid_to_str(svc->uuid, uuid_str, sizeof(uuid_str));
	printk("SVC %s handles 0x%04x..0x%04x%s\n", uuid_str, attr->handle,
	       svc->end_handle, hids ? "   <<< HID SERVICE PRESENT >>>" : "");
	return BT_GATT_ITER_CONTINUE;
}

/* --- re-enumerate on Service Changed (catch a dynamic HIDS reveal) -------- */

static struct bt_gatt_discover_params reenum_disc;
static int reenum_count;

static uint8_t reenum_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 struct bt_gatt_discover_params *params)
{
	if (attr == NULL) {
		printk("=== re-enumeration complete ===\n");
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_service_val *svc = attr->user_data;
	char uuid_str[BT_UUID_STR_LEN];
	bool hids = (bt_uuid_cmp(svc->uuid, BT_UUID_HIDS) == 0);

	bt_uuid_to_str(svc->uuid, uuid_str, sizeof(uuid_str));
	printk("  [re-enum] SVC %s 0x%04x..0x%04x%s\n", uuid_str, attr->handle,
	       svc->end_handle, hids ? "   <<< HID SERVICE APPEARED >>>" : "");
	return BT_GATT_ITER_CONTINUE;
}

static void reenumerate(void)
{
	if (ctx.conn == NULL || reenum_count++ > 8) {
		return;
	}

	reenum_disc.uuid = NULL;
	reenum_disc.func = reenum_cb;
	reenum_disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	reenum_disc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	reenum_disc.type = BT_GATT_DISCOVER_PRIMARY;

	if (bt_gatt_discover(ctx.conn, &reenum_disc) != 0) {
		printk("  re-enum discover failed\n");
	}
}

/* --- public entry points -------------------------------------------------- */

void hogp_start(struct bt_conn *conn)
{
	/* Safe to reset now: any prior connection's subscriptions were
	 * volatile and already removed by the host on its disconnect.
	 */
	hogp_stop();
	memset(&ctx, 0, sizeof(ctx));
	ctx.conn = conn;
	reenum_count = 0;

	ctx.disc.uuid = NULL; /* all primary services */
	ctx.disc.func = svc_cb;
	ctx.disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	ctx.disc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	ctx.disc.type = BT_GATT_DISCOVER_PRIMARY;

	if (bt_gatt_discover(conn, &ctx.disc) != 0) {
		printk("HOGP/sniff: service discovery failed\n");
		return;
	}
	printk("HOGP/sniff: enumerating GATT DB...\n");
}

void hogp_stop(void)
{
	/* Release keys only. Do NOT memset here -- on disconnect the host may
	 * still be tearing down our (volatile) subscriptions; wiping the
	 * params underneath it is the PC=0 crash. hogp_start does the reset.
	 */
	kbd_report_release_all(&ctx.kbd, key_event, NULL);
}
