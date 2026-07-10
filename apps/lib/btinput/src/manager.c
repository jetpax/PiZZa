/*
 * apps/lib/btinput -- connection manager (CONFIG_BTINPUT_MANAGER).
 *
 * The connection policy proven on HW during BT bring-up (rpi_zero_2w /
 * CYW43436 / 8BitDo Micro + MX Master 2S), packaged so a consumer app calls
 * btinput_manager_start() once and gets working BT HID input. Everything
 * below the policy line (security, HID channels, parsing, sinks) is owned by
 * hid_host_br.c / hid_host_le.c already; this file owns WHEN to page, scan
 * and attach.
 *
 * Policy, all HW-earned (memories project_wo_bt_k1_m0_bringup,
 * reference_zephyr_classic_hid_host):
 *
 *  - Bond storage: FAT mount + settings FILE backend. A failed mount
 *    degrades to RAM-only bonds and an ephemeral identity, never blocks
 *    the radio work.
 *  - Classic reconnection is DEVICE-initiated, but the 8BitDo's own inbound
 *    attempts die ~195 ms after ACL-up more often than not, while
 *    host-initiated pages complete reliably. So: inbound preferred, with an
 *    outbound RESCUE page per bonded device -- boot +2 s, re-armed 1.2 s
 *    after every disconnect (deliberately off the pad's own 2-3 s retry
 *    beat), 3.5 s after a failed page; an inbound connect cancels it.
 *  - First pairing (no Classic bonds yet): one BR inquiry window at boot;
 *    the first discoverable HID-class device (CoD peripheral major with the
 *    keyboard/pointing minor bits) gets paged, paired (SSP Just Works) and
 *    bonded. Paging happens from a work item -- bt_conn_create_br() from the
 *    discovery callback would block the BT RX path.
 *  - LE (CONFIG_BTINPUT_HOGP): scan starts after the inquiry window (they
 *    contend for the radio), connects one advertiser at a time -- bonded
 *    peers or HID-flavored ones (HIDS UUID / HID appearance / known names) --
 *    secures it and hands it to btinput_le_attach(). A short post-attach
 *    probe blocklists peers that never armed input (the 8BitDo's LE face is
 *    a vendor config service with no HIDS; reconnecting it forever starved
 *    the real mouse). Security failures against a bonded peer drop the stale
 *    bond (the device re-paired elsewhere); failures back off 3 s so retries
 *    don't die in supervision timeout.
 */

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/classic/classic.h>

#ifdef CONFIG_SETTINGS
#include <zephyr/settings/settings.h>
#endif
#ifdef CONFIG_SETTINGS_FILE
#include <zephyr/fs/fs.h>
#include <ff.h>
#endif

#include "btinput.h"

LOG_MODULE_REGISTER(btinput_mgr, CONFIG_BTINPUT_LOG_LEVEL);

#define UUID16_HIDS 0x1812
/* GAP appearance: the whole HID category (0x03c0 generic .. 0x03c8
 * digitizer); the MX Master advertises as mouse (0x03c2).
 */
#define APPEARANCE_IS_HID(a) ((a) >= 0x03c0 && (a) <= 0x03cf)

/* CoD: peripheral major + keyboard/pointing minor bits. */
#define COD_MAJOR(cod)      (((cod) >> 8) & 0x1f)
#define COD_MAJOR_PERIPH    0x05
#define COD_MINOR_HID_BITS  0xc0

/* --- bond storage --------------------------------------------------------- */

static bool settings_ok;

#ifdef CONFIG_SETTINGS_FILE
static FATFS mgr_fat;
static struct fs_mount_t mgr_mount = {
	.type = FS_FATFS,
	.fs_data = &mgr_fat,
	.mnt_point = CONFIG_BTINPUT_MANAGER_MOUNT,
};

static void storage_init(void)
{
	struct fs_statvfs vstat;
	int rc;

	/* Probe before mounting: fs_mount on an already-mounted point logs
	 * a loud error before returning -EBUSY, and a consumer app owning
	 * the volume (minivmac's disk) is the normal case, not a fault.
	 */
	if (fs_statvfs(CONFIG_BTINPUT_MANAGER_MOUNT, &vstat) == 0) {
		LOG_DBG("%s already mounted by the app",
			CONFIG_BTINPUT_MANAGER_MOUNT);
	} else {
		rc = fs_mount(&mgr_mount);
		if (rc != 0 && rc != -EBUSY) {
			LOG_WRN("mount %s failed (%d) -- bonds RAM-only this boot",
				CONFIG_BTINPUT_MANAGER_MOUNT, rc);
			return;
		}
	}

	/* Ensure the settings file's directory exists and pre-create the
	 * file: the backend probes it with fs_open on every load and logs
	 * errors on a virgin card. Stat-before-mkdir keeps boots quiet.
	 */
	static char dir[64];
	const char *slash = strrchr(CONFIG_SETTINGS_FILE_PATH, '/');
	struct fs_dirent ent;

	if (slash != NULL &&
	    (size_t)(slash - CONFIG_SETTINGS_FILE_PATH) < sizeof(dir)) {
		memcpy(dir, CONFIG_SETTINGS_FILE_PATH,
		       slash - CONFIG_SETTINGS_FILE_PATH);
		dir[slash - CONFIG_SETTINGS_FILE_PATH] = '\0';

		if (fs_stat(dir, &ent) == -ENOENT && fs_mkdir(dir) != 0) {
			LOG_WRN("mkdir %s failed -- bonds RAM-only", dir);
			return;
		}
	}

	if (fs_stat(CONFIG_SETTINGS_FILE_PATH, &ent) == -ENOENT) {
		struct fs_file_t f;

		fs_file_t_init(&f);
		if (fs_open(&f, CONFIG_SETTINGS_FILE_PATH,
			    FS_O_CREATE | FS_O_WRITE) == 0) {
			fs_close(&f);
		}
	}

	settings_ok = true;
	LOG_INF("bond store at " CONFIG_SETTINGS_FILE_PATH);
}
#else
static void storage_init(void)
{
}
#endif /* CONFIG_SETTINGS_FILE */

/* --- radio interlock ---------------------------------------------------------
 * One radio-affecting op at a time: LE connect establishment and BR paging
 * contend at the controller (0x3e failed-to-establish class, and worse).
 * br_paging spans create -> resolution in mgr_connected(); le_busy() is true
 * while an LE connect is being established (no-op without BTINPUT_HOGP).
 */
static bool br_paging;
static bool le_busy(void);

/* --- Classic: per-device rescue-page watches -------------------------------- */

struct br_watch {
	bt_addr_t addr;
	struct bt_conn *conn;   /* our ref: page-created or inbound-claimed */
	struct k_work_delayable page_work;
	bool used;
	bool bonded;
};

static struct br_watch watches[CONFIG_BTINPUT_MAX_DEVICES];

static struct br_watch *watch_find(const bt_addr_t *addr)
{
	for (int i = 0; i < ARRAY_SIZE(watches); i++) {
		if (watches[i].used && bt_addr_eq(&watches[i].addr, addr)) {
			return &watches[i];
		}
	}
	return NULL;
}

static struct br_watch *watch_claim(const bt_addr_t *addr)
{
	struct br_watch *w = watch_find(addr);

	if (w != NULL) {
		return w;
	}
	for (int i = 0; i < ARRAY_SIZE(watches); i++) {
		if (!watches[i].used) {
			w = &watches[i];
			w->used = true;
			w->addr = *addr;
			w->conn = NULL;
			w->bonded = false;
			return w;
		}
	}
	return NULL;
}

static void page_fn(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct br_watch *w = CONTAINER_OF(dwork, struct br_watch, page_work);
	char str[BT_ADDR_STR_LEN];

	/* An inbound connect landed first (it cancels us, but a race is
	 * possible) -- or the slot was torn down.
	 */
	if (!w->used || w->conn != NULL) {
		return;
	}

	/* One radio op at a time: wait out an LE connect establishment. */
	if (le_busy()) {
		k_work_schedule(&w->page_work, K_MSEC(700));
		return;
	}

	bt_addr_to_str(&w->addr, str, sizeof(str));
	LOG_INF("paging %s%s", str, w->bonded ? " (rescue)" : " (first pair)");

	br_paging = true;
	w->conn = bt_conn_create_br(&w->addr, BT_BR_CONN_PARAM_DEFAULT);
	if (w->conn == NULL) {
		br_paging = false;
		LOG_WRN("page create failed; retry in 3.5 s");
		k_work_schedule(&w->page_work, K_MSEC(3500));
	}
	/* Success/failure of the page itself lands in mgr_connected(). */
}

/* --- Classic: first-pair inquiry -------------------------------------------- */

static void le_scan_start(void); /* no-op without CONFIG_BTINPUT_HOGP */

static struct bt_br_discovery_result disc_results[8];

static void disc_timeout(const struct bt_br_discovery_result *results,
			 size_t count)
{
	char str[BT_ADDR_STR_LEN];
	bool paged = false;

	for (size_t i = 0; i < count; i++) {
		uint32_t cod = sys_get_le24(results[i].cod);
		bool hid_class = COD_MAJOR(cod) == COD_MAJOR_PERIPH &&
				 (cod & COD_MINOR_HID_BITS) != 0;

		bt_addr_to_str(&results[i].addr, str, sizeof(str));
		LOG_INF("INQ %s CoD 0x%06x %d dBm%s", str, cod,
			results[i].rssi, hid_class ? "  [HID]" : "");

		if (hid_class && !paged) {
			struct br_watch *w = watch_claim(&results[i].addr);

			if (w != NULL) {
				paged = true;
				/* Page from the workqueue: create_br here
				 * would block the BT RX path.
				 */
				k_work_schedule(&w->page_work, K_NO_WAIT);
			}
		}
	}
	if (count == 0) {
		LOG_INF("inquiry: nothing discoverable (put the device in "
			"pairing mode and reboot to pair)");
	}

	/* Inquiry over -- the LE scan may have the radio now. */
	le_scan_start();
}

static struct bt_br_discovery_cb disc_cb = {
	.timeout = disc_timeout,
};

/* --- LE: scan / connect / attach policy -------------------------------------- */

#ifdef CONFIG_BTINPUT_HOGP

/* Advertised names that mark a HID device even when the AD carries no HIDS
 * UUID or appearance (the MX Master's pairing ADV needed this on HW).
 */
static const char *const le_name_prefixes[] = { "MX", "8BitDo" };

static struct bt_conn *le_conn;
static bool le_connecting;
static int64_t le_retry_after;

/* LE connects run from a work item, never from the scan callback: the scan
 * callback executes on the BT RX workqueue, and bt_le_scan_stop() there
 * parks that thread in bt_hci_cmd_send_sync -- on HW (2026-07-08, minivmac
 * image) the controller never answered a 13 ms scan enable->disable toggle
 * and the 10 s command timeout took the whole stack down with a kernel oops
 * on the BT RX WQ. Same rule as paging from the inquiry callback.
 */
static struct k_work_delayable le_connect_work;
static bt_addr_le_t le_target;

/* True while an LE connection is being established (scan match -> connected
 * callback); the BR rescue page waits it out.
 */
static bool le_busy(void)
{
	return le_connecting;
}

/* Peers whose LE face attached but never armed input (no HIDS): don't
 * reconnect them this boot. Ring of the last few verdicts.
 */
static bt_addr_le_t nohid[4];
static int nohid_next;

/* Post-attach probe: if HIDS discovery hasn't armed input a few seconds in,
 * the peer isn't a HID device on this face -- blocklist + disconnect. Fires
 * at 3 s: comfortably after discovery (<1 s), before the 8BitDo LE face's
 * ~4 s idle-drop.
 */
static struct k_work_delayable le_probe_work;

static bool le_nohid_has(const bt_addr_le_t *addr)
{
	for (int i = 0; i < ARRAY_SIZE(nohid); i++) {
		if (bt_addr_le_eq(&nohid[i], addr)) {
			return true;
		}
	}
	return false;
}

static void le_probe_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (le_conn == NULL || btinput_le_armed(le_conn)) {
		return;
	}

	const bt_addr_le_t *addr = bt_conn_get_dst(le_conn);
	char str[BT_ADDR_LE_STR_LEN];

	if (addr != NULL && !le_nohid_has(addr)) {
		nohid[nohid_next] = *addr;
		nohid_next = (nohid_next + 1) % ARRAY_SIZE(nohid);
	}
	bt_addr_le_to_str(addr, str, sizeof(str));
	LOG_INF("%s never armed HID input -- not a HID LE face, dropping", str);
	bt_conn_disconnect(le_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

/* Bonded-address cache (identity addresses; the host resolves RPAs). */
static bt_addr_le_t le_bonded[CONFIG_BT_MAX_PAIRED];
static int le_bonded_count;

static void le_bond_cb(const struct bt_bond_info *info, void *user_data)
{
	ARG_UNUSED(user_data);

	if (le_bonded_count < ARRAY_SIZE(le_bonded)) {
		le_bonded[le_bonded_count++] = info->addr;
	}
}

static void le_bond_cache_load(void)
{
	le_bonded_count = 0;
	bt_foreach_bond(BT_ID_DEFAULT, le_bond_cb, NULL);
	LOG_INF("%d LE bond(s)", le_bonded_count);
}

static bool le_is_bonded(const bt_addr_le_t *addr)
{
	for (int i = 0; i < le_bonded_count; i++) {
		if (bt_addr_le_eq(&le_bonded[i], addr)) {
			return true;
		}
	}
	return false;
}

static void le_bond_drop(const bt_addr_le_t *addr)
{
	for (int i = 0; i < le_bonded_count; i++) {
		if (bt_addr_le_eq(&le_bonded[i], addr)) {
			le_bonded[i] = le_bonded[--le_bonded_count];
			return;
		}
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

static void le_connect_fn(struct k_work *work)
{
	char str[BT_ADDR_LE_STR_LEN];
	int err;

	ARG_UNUSED(work);

	if (le_conn != NULL) {
		le_connecting = false;
		return;
	}

	/* Wait out an in-flight BR page; le_connecting stays set so the
	 * scan callback doesn't re-arm us with a different target.
	 */
	if (br_paging) {
		k_work_schedule(&le_connect_work, K_MSEC(600));
		return;
	}

	err = bt_le_scan_stop();
	if (err && err != -EALREADY) {
		LOG_WRN("scan stop failed (%d)", err);
		le_connecting = false;
		return;
	}

	bt_addr_le_to_str(&le_target, str, sizeof(str));
	LOG_INF("LE connecting to %s", str);
	err = bt_conn_le_create(&le_target, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &le_conn);
	if (err) {
		LOG_WRN("LE create failed (%d)", err);
		le_connecting = false;
		le_scan_start();
	}
}

static void le_scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		       struct net_buf_simple *ad)
{
	struct adv_summary s = { 0 };
	bool hid_like, bonded;

	ARG_UNUSED(rssi);

	if (le_connecting || le_conn != NULL) {
		return;
	}
	if (k_uptime_get() < le_retry_after) {
		return;
	}
	if (adv_type == BT_GAP_ADV_TYPE_ADV_NONCONN_IND ||
	    adv_type == BT_GAP_ADV_TYPE_ADV_SCAN_IND) {
		return;
	}

	bt_data_parse(ad, ad_parse_cb, &s);

	hid_like = s.hid_uuid || APPEARANCE_IS_HID(s.appearance);
	for (int i = 0; !hid_like && i < ARRAY_SIZE(le_name_prefixes); i++) {
		hid_like = strncmp(s.name, le_name_prefixes[i],
				   strlen(le_name_prefixes[i])) == 0;
	}
	bonded = le_is_bonded(addr);

	if ((hid_like || bonded) && !le_nohid_has(addr)) {
		/* Hand off to the workqueue (see le_connect_fn); the small
		 * delay also keeps a freshly-enabled scan from being toggled
		 * straight back off, which is what wedged the controller.
		 */
		le_connecting = true;
		le_target = *addr;
		k_work_schedule(&le_connect_work, K_MSEC(80));
	}
}

static void le_scan_start(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, le_scan_cb);

	if (err && err != -EALREADY) {
		LOG_WRN("LE scan start failed (%d)", err);
		return;
	}
	LOG_INF("LE scan running (HID-flavored or bonded advertisers)");
}

#else /* !CONFIG_BTINPUT_HOGP */

static void le_bond_cache_load(void)
{
}
static void le_scan_start(void)
{
}
static bool le_busy(void)
{
	return false;
}

#endif /* CONFIG_BTINPUT_HOGP */

/* --- connection lifecycle ------------------------------------------------------ */

static bool conn_is_br(struct bt_conn *conn, bt_addr_t *dst)
{
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) != 0 ||
	    info.type != BT_CONN_TYPE_BR) {
		return false;
	}
	if (dst != NULL) {
		*dst = *info.br.dst;
	}
	return true;
}

static void mgr_connected(struct bt_conn *conn, uint8_t err)
{
	bt_addr_t dst;

	if (conn_is_br(conn, &dst)) {
		struct br_watch *w = watch_find(&dst);

		if (err) {
			if (w != NULL && w->conn == conn) {
				/* Our page failed. Bonded devices get the
				 * forever-retry (the pad's own attempts may
				 * be storming; 3.5 s stays off its beat);
				 * a first-pair page is one-shot.
				 */
				br_paging = false;
				bt_conn_unref(w->conn);
				w->conn = NULL;
				if (w->bonded) {
					k_work_schedule(&w->page_work,
							K_MSEC(3500));
				} else {
					w->used = false;
				}
			}
			return;
		}

		if (w == NULL) {
			/* Inbound from a device we don't track yet
			 * (first-pair initiated by the device).
			 */
			w = watch_claim(&dst);
		}
		if (w != NULL) {
			if (w->conn == conn) {
				/* Our page completed. */
				br_paging = false;
			} else if (w->conn == NULL) {
				w->conn = bt_conn_ref(conn);
			}
			/* Inbound landed: retire any pending rescue page. */
			k_work_cancel_delayable(&w->page_work);
		}
		/* Security + HID channels are hid_host_br.c's job. */
		return;
	}

#ifdef CONFIG_BTINPUT_HOGP
	if (le_conn != NULL && conn == le_conn) {
		le_connecting = false;
		if (err) {
			/* 0x3e failed-to-establish under BR page contention
			 * heals on retry (HW 2026-07-08).
			 */
			LOG_WRN("LE connect failed (0x%02x)", err);
			bt_conn_unref(le_conn);
			le_conn = NULL;
			le_scan_start();
			return;
		}
		/* Secure it; attach happens in security_changed. */
		if (bt_conn_set_security(conn, BT_SECURITY_L2) < 0) {
			LOG_WRN("LE set_security failed");
			bt_conn_disconnect(conn,
					   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
	}
#endif
}

static void mgr_disconnected(struct bt_conn *conn, uint8_t reason)
{
	bt_addr_t dst;

	ARG_UNUSED(reason);

	if (conn_is_br(conn, &dst)) {
		struct br_watch *w = watch_find(&dst);

		if (w == NULL || w->conn != conn) {
			return;
		}
		bt_conn_unref(w->conn);
		w->conn = NULL;

		if (w->bonded) {
			/* Re-arm the rescue. 1.2 s, deliberately NOT 2-3 s:
			 * that matched the pad's own retry cadence and paged
			 * into its storm forever; aim for the listen window
			 * right after its attempt dies. An inbound connect
			 * cancels this.
			 */
			k_work_schedule(&w->page_work, K_MSEC(1200));
		} else {
			w->used = false;
		}
		return;
	}

#ifdef CONFIG_BTINPUT_HOGP
	if (conn == le_conn) {
		k_work_cancel_delayable(&le_probe_work);
		bt_conn_unref(le_conn);
		le_conn = NULL;
		le_connecting = false;
		le_scan_start();
	}
#endif
}

static void mgr_security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err)
{
	ARG_UNUSED(level);
	ARG_UNUSED(err);

	if (conn_is_br(conn, NULL)) {
		return; /* BR security outcomes are hid_host_br.c policy */
	}

#ifdef CONFIG_BTINPUT_HOGP
	if (conn != le_conn) {
		return;
	}

	if (err) {
		const bt_addr_le_t *addr = bt_conn_get_dst(conn);

		LOG_WRN("LE security failed (err %d)", err);

		/* A bonded peer that rejects our key re-paired elsewhere:
		 * drop the stale bond so the next connect pairs fresh.
		 */
		if (addr != NULL && le_is_bonded(addr)) {
			LOG_INF("stale LE bond -- unpairing for a fresh pair");
			bt_unpair(BT_ID_DEFAULT, addr);
			le_bond_drop(addr);
		}

		le_retry_after = k_uptime_get() + 3000;
		bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
		return;
	}

	int aerr = btinput_le_attach(conn);

	if (aerr != 0 && aerr != -EALREADY) {
		LOG_WRN("le_attach failed (%d)", aerr);
		return;
	}
	k_work_schedule(&le_probe_work, K_SECONDS(3));
#endif
}

BT_CONN_CB_DEFINE(btinput_mgr_conn_cb) = {
	.connected = mgr_connected,
	.disconnected = mgr_disconnected,
	.security_changed = mgr_security_changed,
};

/* --- pairing outcomes ------------------------------------------------------- */

static void mgr_pairing_complete(struct bt_conn *conn, bool bonded)
{
	bt_addr_t dst;

	if (conn_is_br(conn, &dst)) {
		struct br_watch *w = watch_claim(&dst);

		if (w != NULL && bonded) {
			w->bonded = true;
			LOG_INF("Classic HID device bonded -- rescue page armed");
		}
		return;
	}

#ifdef CONFIG_BTINPUT_HOGP
	const bt_addr_le_t *addr = bt_conn_get_dst(conn);

	if (bonded && addr != NULL && !le_is_bonded(addr) &&
	    le_bonded_count < ARRAY_SIZE(le_bonded)) {
		le_bonded[le_bonded_count++] = *addr;
	}
#endif
}

static void mgr_pairing_failed(struct bt_conn *conn,
			       enum bt_security_err reason)
{
	ARG_UNUSED(conn);
	LOG_WRN("pairing failed (reason %d)", reason);
#ifdef CONFIG_BTINPUT_HOGP
	/* LE-only cooldown; BR has the rescue-page policy. */
	if (!conn_is_br(conn, NULL)) {
		le_retry_after = k_uptime_get() + 5000;
	}
#endif
}

static struct bt_conn_auth_info_cb mgr_auth_info_cb = {
	.pairing_complete = mgr_pairing_complete,
	.pairing_failed = mgr_pairing_failed,
};

/* --- bring-up thread ---------------------------------------------------------- */

static void br_bond_cb(const struct bt_br_bond_info *info, void *user_data)
{
	int *count = user_data;
	struct br_watch *w = watch_claim(&info->addr);

	if (w != NULL) {
		w->bonded = true;
		(*count)++;
	}
}

static void mgr_thread_fn(void *p1, void *p2, void *p3)
{
	int br_bonds = 0;
	int err;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	storage_init();

	err = bt_enable(NULL);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("bt_enable failed (%d) -- no BT input", err);
		return;
	}

#ifdef CONFIG_SETTINGS
	if (settings_ok) {
		err = settings_subsys_init();
		if (err == 0) {
			err = settings_load();
		}
		if (err) {
			LOG_WRN("settings load failed (%d) -- bonds RAM-only", err);
			settings_ok = false;
		}
	}
#endif
	if (!settings_ok) {
		/* No persisted identity: create an ephemeral one so the
		 * central role still works this boot.
		 */
		err = bt_id_create(NULL, NULL);
		if (err < 0 && err != -EALREADY) {
			LOG_WRN("bt_id_create failed (%d)", err);
		}
	}

	err = btinput_init();
	if (err) {
		LOG_ERR("btinput_init failed (%d)", err);
		return;
	}

	bt_conn_auth_info_cb_register(&mgr_auth_info_cb);
	le_bond_cache_load();
	bt_br_foreach_bond(br_bond_cb, &br_bonds);
	LOG_INF("%d Classic bond(s)", br_bonds);

	if (br_bonds == 0) {
		/* First pairing: one inquiry window (~10 s), then the LE
		 * scan starts from its completion (radio contention).
		 */
		struct bt_br_discovery_param dp = {
			.length = 8,
			.limited = false,
		};

		bt_br_discovery_cb_register(&disc_cb);
		err = bt_br_discovery_start(&dp, disc_results,
					    ARRAY_SIZE(disc_results));
		if (err) {
			LOG_WRN("inquiry failed (%d)", err);
			le_scan_start();
		} else {
			LOG_INF("no Classic bonds -- inquiry (~10 s); put the "
				"device in pairing mode");
		}
	} else {
		/* Bonded devices reconnect device-initiated; arm the rescue
		 * pages after a grace period (staggered per device) and let
		 * the LE scan run now.
		 */
		int i = 0;

		for (int j = 0; j < ARRAY_SIZE(watches); j++) {
			if (watches[j].used && watches[j].bonded) {
				k_work_schedule(&watches[j].page_work,
						K_SECONDS(2 + 2 * i));
				i++;
			}
		}
		le_scan_start();
	}

	LOG_INF("manager up (%s bonds)",
		settings_ok ? "persistent" : "RAM-only");
}

static K_THREAD_STACK_DEFINE(mgr_stack, 8192);
static struct k_thread mgr_thread;

int btinput_manager_start(void)
{
	static bool started;

	if (started) {
		return -EALREADY;
	}
	started = true;

	for (int i = 0; i < ARRAY_SIZE(watches); i++) {
		k_work_init_delayable(&watches[i].page_work, page_fn);
	}
#ifdef CONFIG_BTINPUT_HOGP
	k_work_init_delayable(&le_probe_work, le_probe_fn);
	k_work_init_delayable(&le_connect_work, le_connect_fn);
#endif

	/* Own thread: bt_enable + the 46 KiB patchram download at 115200
	 * take seconds -- the consumer (a game) boots in parallel.
	 */
	k_thread_create(&mgr_thread, mgr_stack, K_THREAD_STACK_SIZEOF(mgr_stack),
			mgr_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&mgr_thread, "btinput_mgr");
	return 0;
}
