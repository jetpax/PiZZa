/*
 * apps/lib/btinput -- reusable Bluetooth HID input for PiZZa apps.
 *
 * A minimal Classic BT (BR/EDR) HID host: Zephyr ships the BR/EDR ACL + SSP +
 * L2CAP machinery but no HID host profile, so this provides it (~150 lines
 * over two L2CAP channels). Boot keyboard reports are diffed to clean key
 * up/down events and handed to a consumer-registered sink -- the SDL /
 * termdirect seam, or a test print. Any PiZZa app links this the same way it
 * links apps/lib/sdl2shim:
 *
 *   CMakeLists.txt: include(../lib/btinput/btinput.cmake)
 *                   target_sources(app PRIVATE ${BTINPUT_SOURCES})
 *                   target_include_directories(app PRIVATE ${BTINPUT_INCLUDE_DIRS})
 *   prj.conf:       CONFIG_BTINPUT=y
 *                   CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB="firmware/<patchram>.hcd"
 *   code:           btinput_init();   (once, after bt_enable())
 *
 * HW-proven on rpi_zero_2w / CYW43436 / 8BitDo Micro (K mode). See the memory
 * reference_zephyr_classic_hid_host for the hard-earned rules.
 */

#ifndef BTINPUT_H_
#define BTINPUT_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/bluetooth/conn.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Key event sink. @p usage is the HID usage code (letters/keys as the boot
 * keyboard reports them; modifiers arrive as usages 0xE0..0xE7). @p pressed
 * is true for a down edge, false for up. A NULL sink drops events.
 */
typedef void (*btinput_key_cb_t)(uint8_t usage, bool pressed, void *user);

/** Register the sink for key events. Safe to call before or after init. */
void btinput_set_key_cb(btinput_key_cb_t cb, void *user);

/**
 * Bring up the Classic (BR/EDR) HID host: register the PSM 0x11 (control) +
 * 0x13 (interrupt) L2CAP servers and make the host connectable/page-scannable,
 * accepting inbound HID links as CENTRAL. Bonded HID devices reconnect
 * device-initiated (they page the host and open the channels themselves); the
 * host routes them internally. Call once after bt_enable(). Returns 0 on
 * success, negative errno otherwise.
 */
int btinput_init(void);

/**
 * Open the HID control+interrupt channels on a freshly-secured OUTBOUND
 * BR/EDR link (first pairing, or a manual re-page). Inbound reconnects need
 * no call -- the device opens the channels and the host routes them.
 */
void btinput_hid_start(struct bt_conn *conn);

/** ACL gone: synthesize UP for every held key and forget channel state. */
void btinput_hid_stop(void);

/** True while a HID channel is up or being set up on the current ACL. */
bool btinput_hid_active(void);

#ifdef __cplusplus
}
#endif

#endif /* BTINPUT_H_ */
