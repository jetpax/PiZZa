/*
 * WO-BT-K1 — M2: LE central + Just Works bonding to the 8BitDo Micro (K mode).
 *
 * History: M0 transport + M1 patchram (SYN43430A1) passed on HW 2026-07-08.
 *
 * Flow: mount the SD FAT (bond storage; degrades to RAM-only bonds if the
 * mount fails) -> bt_enable -> settings_load -> active scan. Every unique
 * advertiser is printed once; anything HID-flavored (HIDS 0x1812 in the AD,
 * keyboard appearance, an "8BitDo" name, or a bonded address) triggers a
 * connection + security elevation. Just Works pairing: no IO capabilities
 * are registered. Bonds persist via the settings file on /SD:.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net_buf.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/classic/classic.h>

#include "hogp.h"
#include "btinput.h"

#define HCI_OP_READ_LOCAL_NAME 0x0c14

#define UUID16_HIDS            0x1812
#define APPEARANCE_HID_GENERIC 0x03c0
#define APPEARANCE_HID_KBD     0x03c1

/* --- SD FAT mount for the settings file backend ------------------------- */

static FATFS fat_fs;
static struct fs_mount_t sd_mount = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

static bool settings_ok;

static void storage_init(void)
{
	int rc = fs_mount(&sd_mount);

	if (rc != 0) {
		printk("SD mount failed (%d) -- bonds will NOT persist this boot\n", rc);
		return;
	}

	/* Stat-before-mkdir: the fs layer logs an error for -EEXIST before
	 * returning it, so a blind mkdir is noisy on every boot but the first.
	 */
	struct fs_dirent ent;

	if (fs_stat("/SD:/bt", &ent) == -ENOENT) {
		rc = fs_mkdir("/SD:/bt");
		if (rc != 0) {
			printk("fs_mkdir(/SD:/bt) failed (%d) -- bonds will NOT persist\n", rc);
			return;
		}
	}

	/* Pre-create the settings file: the backend probes it with fs_open on
	 * every load and logs an -ENOENT error on a virgin card.
	 */
	if (fs_stat(CONFIG_SETTINGS_FILE_PATH, &ent) == -ENOENT) {
		struct fs_file_t f;

		fs_file_t_init(&f);
		rc = fs_open(&f, CONFIG_SETTINGS_FILE_PATH, FS_O_CREATE | FS_O_WRITE);
		if (rc == 0) {
			fs_close(&f);
		}
	}

	settings_ok = true;
	printk("SD mounted; bond store at " CONFIG_SETTINGS_FILE_PATH "\n");
}

/* --- controller identification (report evidence) ------------------------ */

static void log_local_version(void)
{
	struct bt_hci_rp_read_local_version_info *v;
	struct net_buf *rsp;
	int err;

	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_LOCAL_VERSION_INFO, NULL, &rsp);
	if (err) {
		printk("  Read_Local_Version failed (err %d)\n", err);
		return;
	}

	v = (void *)rsp->data;
	printk("  HCI v%u rev 0x%04x | LMP v%u subver 0x%04x | manufacturer %u\n",
	       v->hci_version, sys_le16_to_cpu(v->hci_revision),
	       v->lmp_version, sys_le16_to_cpu(v->lmp_subversion),
	       sys_le16_to_cpu(v->manufacturer));
	net_buf_unref(rsp);
}

static void log_local_name(void)
{
	struct net_buf *rsp;
	int err;

	err = bt_hci_cmd_send_sync(HCI_OP_READ_LOCAL_NAME, NULL, &rsp);
	if (err) {
		printk("  Read_Local_Name failed (err %d)\n", err);
		return;
	}

	printk("  Controller: \"%s\"\n", (const char *)&rsp->data[1]);
	net_buf_unref(rsp);
}

/* --- bonded-address cache (identity addresses; host resolves RPAs) ------ */

static bt_addr_le_t bonded_addrs[CONFIG_BT_MAX_PAIRED];
static int bonded_count;

static void bond_cache_cb(const struct bt_bond_info *info, void *user_data)
{
	char str[BT_ADDR_LE_STR_LEN];

	if (bonded_count < ARRAY_SIZE(bonded_addrs)) {
		bonded_addrs[bonded_count++] = info->addr;
	}
	bt_addr_le_to_str(&info->addr, str, sizeof(str));
	printk("  bonded peer: %s\n", str);
}

static void bond_cache_load(void)
{
	bonded_count = 0;
	bt_foreach_bond(BT_ID_DEFAULT, bond_cache_cb, NULL);
	if (bonded_count == 0) {
		printk("  no stored bonds\n");
	}
}

static bool is_bonded_addr(const bt_addr_le_t *addr)
{
	for (int i = 0; i < bonded_count; i++) {
		if (bt_addr_le_eq(&bonded_addrs[i], addr)) {
			return true;
		}
	}
	return false;
}

/* --- scanning + pad detection ------------------------------------------- */

static struct bt_conn *pad_conn;
static bool connecting;
static int64_t retry_after;

static void start_scan(void);

/* --- BR/EDR (Classic) path ------------------------------------------------
 * The pad's keyboard is Classic HID; page its Classic BD_ADDR directly.
 * NOTE: this pad does NOT share one address across faces -- macOS System
 * Report shows the Classic identity as E4:17:D8:27:8C:4F (Services
 * "0x800020 <HID ACL>"), while the BLE face is E4:17:D8:A2:EE:8D.
 * LSB-first here.
 */
static const bt_addr_t pad_br_addr = {
	.val = { 0x4f, 0x8c, 0x27, 0xd8, 0x17, 0xe4 },
};
static struct bt_conn *br_conn;
static bool br_inbound;
static struct k_work_delayable br_page_work;
static struct k_work_delayable br_hid_fallback_work;

/* True once a BR link key for the pad exists -- seeded at boot from the bond
 * store, latched on the first successful encrypt.
 *
 * HW truth (2026-07-08, three flashes): the pad's DEVICE-INITIATED inbound
 * reconnects are FLAKY -- most attempts die ~195 ms after the ACL comes up
 * (pad drops 0x13; if our auth/encrypt hasn't finished by then it surfaces
 * as security err 9 mid-SMP) -- while HOST-initiated (outbound page) sessions
 * complete reliably. An inbound-only policy therefore never converges on a
 * bad day; a boot-time blind page races the pad's own storm. Policy:
 * inbound preferred (fastest when it works, and the pad initiates it anyway),
 * outbound page armed as the RESCUE -- boot pages after a short grace,
 * every BR disconnect re-arms a page, and an inbound connect CANCELS the
 * pending page. Why the pad bails at ~195 ms on its own reconnects is an
 * open question (prime suspect: it validates the host, e.g. SDP, and we
 * register no HID-host records) -- parked for M4.x, not fixed by timing.
 */
static bool br_bonded;

/* There is no public "is this BR address bonded?" API (bt_le_bond_exists is
 * LE-only; bt_foreach_bond enumerates LE identities). bt_keys_find_link_key()
 * -- subsys/bluetooth/host/keys.h, linked in with CONFIG_BT_CLASSIC -- is the
 * honest stored-link-key check; forward-declared here rather than reaching
 * into a host-internal header. Returns NULL when the address has no bond.
 */
struct bt_keys_link_key;
extern struct bt_keys_link_key *bt_keys_find_link_key(const bt_addr_t *addr);

static bool br_bond_exists(void)
{
	return bt_keys_find_link_key(&pad_br_addr) != NULL;
}

/* Inbound ACLs: the pad normally opens the HID channels itself. If it
 * hasn't within the grace period, host-initiate them (some devices page in
 * but expect host-opened channels).
 */
static void br_hid_fallback(struct k_work *work)
{
	ARG_UNUSED(work);

	if (br_conn != NULL && !btinput_hid_active()) {
		printk("BR-HID: pad opened no channels; host-initiating\n");
		btinput_hid_start(br_conn);
	}
}

static void br_page(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Covers first pairing AND the bonded rescue (the pad's own inbound
	 * attempts usually die ~195 ms in; a host page always completes).
	 * An inbound connect cancels this work before it fires.
	 */
	if (br_conn != NULL) {
		return;
	}

	printk(">>> BR/EDR paging 8BitDo (E4:17:D8:27:8C:4F)...\n");
	br_conn = bt_conn_create_br(&pad_br_addr, BT_BR_CONN_PARAM_DEFAULT);
	if (br_conn == NULL) {
		printk("bt_conn_create_br failed; retrying in 3 s\n");
		k_work_schedule(&br_page_work, K_SECONDS(3));
	}
}

static const char *sec_err_str(enum bt_security_err err)
{
	switch (err) {
	case BT_SECURITY_ERR_AUTH_FAIL:         return "AUTH_FAIL";
	case BT_SECURITY_ERR_PIN_OR_KEY_MISSING: return "PIN_OR_KEY_MISSING";
	case BT_SECURITY_ERR_OOB_NOT_AVAILABLE: return "OOB_NOT_AVAILABLE";
	case BT_SECURITY_ERR_AUTH_REQUIREMENT:  return "AUTH_REQUIREMENT";
	case BT_SECURITY_ERR_PAIR_NOT_SUPPORTED: return "PAIR_NOT_SUPPORTED";
	case BT_SECURITY_ERR_PAIR_NOT_ALLOWED:  return "PAIR_NOT_ALLOWED";
	case BT_SECURITY_ERR_INVALID_PARAM:     return "INVALID_PARAM";
	case BT_SECURITY_ERR_KEY_REJECTED:      return "KEY_REJECTED";
	case BT_SECURITY_ERR_UNSPECIFIED:       return "UNSPECIFIED";
	default:                                return "?";
	}
}

struct adv_summary {
	char name[32];
	uint16_t appearance;
	bool hid_uuid;
};

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
	struct adv_summary *s = user_data;

	switch (data->type) {
	case BT_DATA_UUID16_SOME:
	case BT_DATA_UUID16_ALL:
		for (int i = 0; i + 1 < data->data_len; i += 2) {
			if (sys_get_le16(&data->data[i]) == UUID16_HIDS) {
				s->hid_uuid = true;
			}
		}
		break;
	case BT_DATA_GAP_APPEARANCE:
		if (data->data_len >= 2) {
			s->appearance = sys_get_le16(data->data);
		}
		break;
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED: {
		size_t n = MIN(data->data_len, sizeof(s->name) - 1);

		memcpy(s->name, data->data, n);
		s->name[n] = '\0';
		break;
	}
	default:
		break;
	}
	return true;
}

/* 8BitDo devices use the E4:17:D8 OUI. The Micro's pairing ADV_IND carries
 * no HID UUID, appearance or name (observed 2026-07-08, -40 dBm on the
 * desk), so the OUI is the only pre-connect fingerprint.
 */
static bool is_8bitdo_oui(const bt_addr_le_t *addr)
{
	return addr->type == BT_ADDR_LE_PUBLIC &&
	       addr->a.val[5] == 0xe4 && addr->a.val[4] == 0x17 &&
	       addr->a.val[3] == 0xd8;
}

/* Print each advertiser once -- plus once more if a later report (e.g. the
 * SCAN_RSP) reveals a name or HID markers the first line lacked.
 */
static struct {
	bt_addr_le_t addr;
	bool info;
} seen[16];
static int seen_next;

static bool seen_before(const bt_addr_le_t *addr, bool info)
{
	for (int i = 0; i < ARRAY_SIZE(seen); i++) {
		if (bt_addr_le_eq(&seen[i].addr, addr)) {
			if (info && !seen[i].info) {
				seen[i].info = true;
				return false;
			}
			return true;
		}
	}
	seen[seen_next].addr = *addr;
	seen[seen_next].info = info;
	seen_next = (seen_next + 1) % ARRAY_SIZE(seen);
	return false;
}

static void connect_to(const bt_addr_le_t *addr)
{
	char str[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(addr, str, sizeof(str));

	err = bt_le_scan_stop();
	if (err) {
		printk("scan stop failed (%d)\n", err);
		return;
	}

	connecting = true;
	printk(">>> connecting to %s\n", str);
	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &pad_conn);
	if (err) {
		printk("bt_conn_le_create failed (%d)\n", err);
		connecting = false;
		start_scan();
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *ad)
{
	static const char *const types[] = {
		"ADV_IND", "DIRECT", "SCAN_IND", "NONCONN", "SCAN_RSP",
	};
	struct adv_summary s = { 0 };
	char str[BT_ADDR_LE_STR_LEN];
	bool pad_like, bonded;

	bt_data_parse(ad, ad_parse_cb, &s);

	pad_like = s.hid_uuid ||
		   s.appearance == APPEARANCE_HID_KBD ||
		   s.appearance == APPEARANCE_HID_GENERIC ||
		   strncmp(s.name, "8BitDo", 6) == 0 ||
		   is_8bitdo_oui(addr);
	bonded = is_bonded_addr(addr);

	if (pad_like || bonded ||
	    !seen_before(addr, s.hid_uuid || s.appearance != 0 || s.name[0] != '\0')) {
		bt_addr_le_to_str(addr, str, sizeof(str));
		printk("ADV %-30s %-8s %4d dBm%s%s%s%s\n", str,
		       adv_type < ARRAY_SIZE(types) ? types[adv_type] : "?",
		       rssi,
		       s.name[0] ? " name=" : "", s.name,
		       s.hid_uuid || s.appearance ? " [HID]" : "",
		       bonded ? " [BONDED]" : "");
	}

	if (connecting || pad_conn != NULL) {
		return;
	}

	/* Post-failure cooldown: give the pad breathing room instead of
	 * re-hammering it every advertising interval (the instant retries
	 * were dying in supervision timeout).
	 */
	if (k_uptime_get() < retry_after) {
		return;
	}

	/* NONCONN / SCAN_IND can't be connected to; anything else HID-ish or
	 * bonded is fair game (the pad's SCAN_RSP implies a connectable ADV_IND).
	 */
	if ((pad_like || bonded) &&
	    adv_type != BT_GAP_ADV_TYPE_ADV_NONCONN_IND &&
	    adv_type != BT_GAP_ADV_TYPE_ADV_SCAN_IND) {
		connect_to(addr);
	}
}

static void start_scan(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);

	if (err) {
		printk("scan start failed (%d)\n", err);
		return;
	}
	printk("Scanning (active)...\n");
}

/* --- connection + security lifecycle ------------------------------------ */

static bool conn_is_br(struct bt_conn *conn)
{
	struct bt_conn_info info;

	return bt_conn_get_info(conn, &info) == 0 &&
	       info.type == BT_CONN_TYPE_BR;
}

/* bt_conn_get_dst() returns NULL for BR/EDR connections (conn.c) -- every
 * address print must go through this type-aware helper instead.
 */
static void conn_addr_str(struct bt_conn *conn, char *str, size_t len)
{
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) != 0) {
		snprintk(str, len, "<no-info>");
		return;
	}

	if (info.type == BT_CONN_TYPE_BR) {
		bt_addr_to_str(info.br.dst, str, len);
	} else {
		bt_addr_le_to_str(info.le.dst, str, len);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char str[BT_ADDR_LE_STR_LEN];
	bool br = conn_is_br(conn);

	conn_addr_str(conn, str, sizeof(str));
	connecting = false;

	if (err) {
		printk("connect to %s (%s) failed (0x%02x)\n", str,
		       br ? "BR" : "LE", err);
		if (br && br_conn != NULL) {
			bt_conn_unref(br_conn);
			br_conn = NULL;
			k_work_schedule(&br_page_work, K_SECONDS(3));
		} else if (pad_conn != NULL) {
			bt_conn_unref(pad_conn);
			pad_conn = NULL;
			start_scan();
		}
		return;
	}

	printk("CONNECTED %s (%s) -- elevating security (Just Works)\n", str,
	       br ? "BR/EDR" : "LE");

	/* Track inbound BR connections (device-initiated reconnect) the same
	 * way as our own pages, so disconnect handling stays uniform.
	 */
	if (br) {
		br_inbound = (br_conn == NULL);
		if (br_inbound) {
			br_conn = bt_conn_ref(conn);
			/* The pad reconnected on its own -- retire any pending
			 * first-pair page so it can't collide with this link.
			 */
			k_work_cancel_delayable(&br_page_work);
		}
	}

	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		printk("set_security failed (%d)\n", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char str[BT_ADDR_LE_STR_LEN];

	conn_addr_str(conn, str, sizeof(str));
	printk("DISCONNECTED %s (reason 0x%02x)\n", str, reason);

	if (br_conn == conn) {
		k_work_cancel_delayable(&br_hid_fallback_work);
		br_inbound = false;
		btinput_hid_stop();
		bt_conn_unref(br_conn);
		br_conn = NULL;
		/* Re-arm the rescue page: if the pad's next inbound attempt dies
		 * its ~195 ms death again, our page grabs it instead. A new
		 * inbound connect cancels this before it fires.
		 */
		k_work_schedule(&br_page_work, K_SECONDS(2));
		return;
	}

	if (pad_conn == conn) {
		hogp_stop();
		bt_conn_unref(pad_conn);
		pad_conn = NULL;
	}
	start_scan();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	if (err) {
		printk("security failed: %s (err %d)\n", sec_err_str(err), err);

		/* LE only: a bonded peer that rejects our key has re-paired
		 * elsewhere (fresh pairing slot, same address). Drop the stale
		 * bond so the next connection pairs from scratch. (R4)
		 * BR link keys live in their own store; just retry there.
		 */
		if (!conn_is_br(conn)) {
			const bt_addr_le_t *addr = bt_conn_get_dst(conn);

			if (addr != NULL && is_bonded_addr(addr)) {
				printk("  stale bond -- unpairing for a fresh pair\n");
				bt_unpair(BT_ID_DEFAULT, addr);
				for (int i = 0; i < bonded_count; i++) {
					if (bt_addr_le_eq(&bonded_addrs[i], addr)) {
						bonded_addrs[i] =
							bonded_addrs[--bonded_count];
						break;
					}
				}
			}
		}

		retry_after = k_uptime_get() + 3000;
		bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
		return;
	}

	printk("ENCRYPTED (L%d, %s) -- bonds %s\n", level,
	       conn_is_br(conn) ? "BR/EDR" : "LE",
	       settings_ok ? "persisted on /SD:" : "RAM ONLY (no SD)");

	/* Act like a HID host now -- Classic HID on BR/EDR (the pad's real
	 * keyboard), GATT sniffer/HOGP on LE. On INBOUND ACLs the pad opens
	 * the channels itself -- initiating ours too collides in L2CAP
	 * signaling ("Idents mismatch", pad drops after ~5 s). Stay passive
	 * with a fallback.
	 */
	if (conn_is_br(conn)) {
		/* Link key established/confirmed. No page work needed while this
		 * link is up (disconnect re-arms the rescue).
		 */
		if (!br_bonded) {
			br_bonded = true;
		}
		k_work_cancel_delayable(&br_page_work);
		if (br_inbound) {
			printk("BR-HID: inbound link secured -- waiting for "
			       "the pad's channel opens\n");
			k_work_schedule(&br_hid_fallback_work, K_MSEC(750));
		} else {
			btinput_hid_start(conn);
		}
	} else {
		hogp_start(conn);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char str[BT_ADDR_LE_STR_LEN];

	conn_addr_str(conn, str, sizeof(str));
	printk("PAIRED with %s (%s, bonded=%d)\n", str,
	       conn_is_br(conn) ? "BR/EDR" : "LE", bonded);

	/* The scan-match cache is LE-only (BR link keys have their own store). */
	if (bonded && !conn_is_br(conn) &&
	    bonded_count < ARRAY_SIZE(bonded_addrs)) {
		bonded_addrs[bonded_count++] = *bt_conn_get_dst(conn);
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	printk("PAIRING FAILED: %s (reason %d)\n", sec_err_str(reason), reason);
	retry_after = k_uptime_get() + 5000;
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* --- input sink ---------------------------------------------------------- */

/* btinput hands every key edge here (the M4 seam will translate to SDL /
 * termdirect; the harness just prints, so HW keymap capture still works).
 */
static void key_print(uint8_t usage, bool pressed, void *user)
{
	ARG_UNUSED(user);
	printk("    KEY 0x%02x %s\n", usage, pressed ? "DOWN" : "UP");
}

/* --- entry --------------------------------------------------------------- */

int main(void)
{
	bt_addr_le_t ids[CONFIG_BT_ID_MAX];
	size_t id_count = ARRAY_SIZE(ids);
	char str[BT_ADDR_LE_STR_LEN];
	int err;

	printk("\n=== WO-BT-K1 M3: HOGP keyboard client (Zero 2W) ===\n");

	storage_init();

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable FAILED (err %d)\n", err);
		return 0;
	}

	printk("bt_enable OK\n");
	log_local_version();
	log_local_name();

	if (settings_ok) {
		err = settings_subsys_init();
		if (err == 0) {
			err = settings_load();
		}
		if (err) {
			printk("settings init/load failed (%d) -- bonds RAM only\n", err);
			settings_ok = false;
		}
	}

	if (!settings_ok) {
		/* No persisted identity; create an ephemeral one so the
		 * central role still works this boot.
		 */
		err = bt_id_create(NULL, NULL);
		if (err < 0 && err != -EALREADY) {
			printk("bt_id_create failed (%d)\n", err);
		}
	}

	bt_id_get(ids, &id_count);
	for (size_t i = 0; i < id_count; i++) {
		bt_addr_le_to_str(&ids[i], str, sizeof(str));
		printk("  identity[%zu]: %s%s\n", i, str,
		       settings_ok ? " (persistent)" : " (ephemeral)");
	}

	bond_cache_load();

	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	if (err) {
		printk("auth_info_cb register failed (%d)\n", err);
	}

	printk("Pad in K mode + pairing mode (hold pair until fast blink);\n");
	printk("Mac Bluetooth OFF (or pad forgotten) so it doesn't steal the link.\n");

	/* Bonded Classic HID devices reconnect DEVICE-INITIATED: on power-on
	 * the pad pages its last host and opens the L2CAP channels itself. The
	 * lib makes us that host -- page-scannable, PSM 0x11/0x13 servers,
	 * inbound accepted as central. Key edges arrive via the sink above.
	 */
	btinput_set_key_cb(key_print, NULL);
	err = btinput_init();
	if (err) {
		printk("btinput_init failed (%d)\n", err);
	}

	k_work_init_delayable(&br_page_work, br_page);
	k_work_init_delayable(&br_hid_fallback_work, br_hid_fallback);

	/* Unbonded: page immediately (first pairing, pad page-scans in pairing
	 * mode). Bonded: give the pad's own inbound reconnect a short head
	 * start, then page as the rescue -- its inbound attempts usually die
	 * ~195 ms in (0x13), while our page reliably completes. An inbound
	 * connect cancels the pending page.
	 */
	br_bonded = br_bond_exists();
	if (br_bonded) {
		printk("Pad is BR-bonded -- inbound preferred, rescue page armed\n");
	} else {
		printk("No BR bond -- paging the pad for first pairing\n");
	}
	k_work_schedule(&br_page_work, br_bonded ? K_SECONDS(2) : K_NO_WAIT);
	return 0;
}
