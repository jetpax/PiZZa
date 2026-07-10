/*
 * apps/lib/btinput -- reusable Bluetooth HID input for PiZZa apps.
 *
 * A minimal Classic BT (BR/EDR) HID host for up to CONFIG_BTINPUT_MAX_DEVICES
 * concurrent devices (keyboard + mouse + combos). Zephyr ships the BR/EDR
 * ACL + SSP + L2CAP machinery but no HID host profile, so this provides it.
 * The lib registers its own connection callbacks and owns the whole BR HID
 * lifecycle: it claims every BR connection (inbound reconnects and
 * consumer-paged first pairings alike), elevates security, opens/accepts the
 * HID channels, parses boot keyboard/mouse reports, and hands clean events to
 * the consumer-registered sinks below. Any PiZZa app links it the same way it
 * links apps/lib/sdl2shim:
 *
 *   CMakeLists.txt: include(../lib/btinput/btinput.cmake)
 *                   target_sources(app PRIVATE ${BTINPUT_SOURCES})
 *                   target_include_directories(app PRIVATE ${BTINPUT_INCLUDE_DIRS})
 *   prj.conf:       CONFIG_BTINPUT=y
 *                   CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB="firmware/<patchram>.hcd"
 *   code:           btinput_set_key_cb(...); btinput_init();  (after bt_enable())
 *
 * Bonded HID devices then reconnect device-initiated with no further consumer
 * code. First pairing is consumer policy: either page the device's Classic
 * address (bt_conn_create_br -- the lib takes the connection over), or make
 * the host discoverable via btinput_pair_mode() for devices that pair
 * host-ward. HW-proven on rpi_zero_2w / CYW43436 / 8BitDo Micro; see the
 * memory reference_zephyr_classic_hid_host for the hard-earned rules.
 */

#ifndef BTINPUT_H_
#define BTINPUT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Key event sink. @p usage is the HID usage code (letters/keys as the boot
 * keyboard reports them; modifiers arrive as usages 0xE0..0xE7). @p pressed
 * is true for a down edge, false for up. A NULL sink drops events.
 */
typedef void (*btinput_key_cb_t)(uint8_t usage, bool pressed, void *user);

/**
 * Mouse button sink. @p button follows boot-report bit order: 0 = left,
 * 1 = right, 2 = middle (higher bits when the device reports them).
 */
typedef void (*btinput_mouse_btn_cb_t)(uint8_t button, bool pressed,
				       void *user);

/**
 * Mouse motion sink: relative deltas from one report (never all-zero --
 * idle keepalives are suppressed). @p wheel is 0 for 3-byte boot reports.
 */
typedef void (*btinput_mouse_move_cb_t)(int dx, int dy, int wheel, void *user);

/** Register the sink for key events. Safe to call before or after init. */
void btinput_set_key_cb(btinput_key_cb_t cb, void *user);

/** Register the sinks for mouse events. Either may be NULL. */
void btinput_set_mouse_cbs(btinput_mouse_btn_cb_t btn_cb,
			   btinput_mouse_move_cb_t move_cb, void *user);

/**
 * Bring up the Classic (BR/EDR) HID host: register the PSM 0x11 (control) +
 * 0x13 (interrupt) L2CAP servers and make the host connectable/page-scannable,
 * accepting inbound HID links as CENTRAL. From here the lib self-drives every
 * BR connection through security, channel setup and teardown. Call once after
 * bt_enable(). Returns 0 on success, negative errno otherwise.
 */
int btinput_init(void);

/**
 * Pairing window: make the host discoverable (inquiry-scannable) so NEW
 * devices that pair host-ward can find it. Reconnection of bonded devices
 * needs only connectable, which btinput_init() already provides. Consumer
 * policy decides the window length.
 */
int btinput_pair_mode(bool enable);

/**
 * LE HOGP backend (CONFIG_BTINPUT_HOGP): hand a connected + SECURED LE
 * connection to the lib. It discovers HIDS, prefers boot protocol (fixed
 * report formats), subscribes the input reports and feeds the same sinks as
 * the Classic path; if the peer has no HIDS it detaches itself. Teardown on
 * disconnect is automatic. LE scan/connect/security policy stays with the
 * consumer (unlike Classic, where the lib owns the whole lifecycle).
 * Returns 0, -EALREADY, -ENOMEM (no free slot), -EIO (discovery failed),
 * or -ENOTSUP without CONFIG_BTINPUT_HOGP.
 */
struct bt_conn;
int btinput_le_attach(struct bt_conn *conn);

/**
 * True once an attached LE connection has finished HIDS discovery and armed
 * its input subscriptions. False for unknown/detached conns (including peers
 * that turned out to have no HIDS). Lets a policy layer probe an attach a few
 * seconds in and blocklist non-HID LE faces (e.g. the 8BitDo's vendor-only
 * BLE side) instead of reconnecting them forever.
 */
bool btinput_le_armed(struct bt_conn *conn);

/* --- SDL seam (CONFIG_BTINPUT_SEAM_SDL; consumer must link sdl2shim) ----- */

/**
 * Optional usage remap for the SDL seam. HID usages already ARE SDL
 * scancodes (SDL2 defines them from the USB HID usage tables), so unmapped
 * usages pass through 1:1; entries here override that for pads whose stock
 * keymap is unhelpful (the 8BitDo Micro K-mode emits letters).
 */
struct btinput_seam_key {
	uint8_t usage;
	uint16_t scancode;
};

/** Install the remap table (static storage; NULL/0 restores identity). */
void btinput_seam_sdl_set_keymap(const struct btinput_seam_key *map,
				 size_t count);

/** Clamp box for the tracked absolute mouse position (default 640x400). */
void btinput_seam_sdl_set_bounds(int w, int h);

/**
 * Register the SDL translator as the btinput sinks: keys, mouse motion
 * (absolute + relative), buttons and wheel all flow into the sdl2shim event
 * queue. Call before or after btinput_init(); replaces earlier sinks.
 */
void btinput_seam_sdl_attach(void);

/* --- connection manager (CONFIG_BTINPUT_MANAGER) ------------------------- */

/**
 * Start the self-contained connection manager on its own thread: mount the
 * FAT + settings bond store, bt_enable(), btinput_init(), then run the
 * HW-proven policy -- boot BR inquiry when nothing is bonded (first pairing;
 * pages any discoverable HID-class device), rescue pages for bonded Classic
 * devices (their device-initiated reconnects are flaky), and with
 * BTINPUT_HOGP an LE scan that connects bonded or HID-flavored advertisers
 * and hands them to btinput_le_attach(). Returns immediately; 0 on success.
 */
int btinput_manager_start(void);

#ifdef __cplusplus
}
#endif

#endif /* BTINPUT_H_ */
