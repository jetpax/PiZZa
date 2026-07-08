/*
 * WO-BT-K1 — minimal Classic BT (BR/EDR) HID host.
 *
 * The 8BitDo Micro's keyboard lives on the Classic HID profile (its BLE face
 * is the vendor config channel only). Zephyr ships BR/EDR ACL + SSP + L2CAP
 * but no HID host profile; this is the missing ~150 lines: L2CAP PSM 0x11
 * (control) + 0x13 (interrupt), input reports arrive on the interrupt
 * channel as 0xA1-prefixed HID reports.
 */

#ifndef BTHID_BR_H_
#define BTHID_BR_H_

#include <zephyr/bluetooth/conn.h>

/* Register L2CAP servers for PSM 0x11/0x13: bonded HID devices reconnect
 * DEVICE-INITIATED (they page the host and open the channels themselves).
 * Call once after bt_enable, together with bt_br_set_connectable(true).
 */
void bthid_br_register(void);

/* Open the HID control+interrupt channels on a secured BR/EDR connection
 * (host-initiated flow: first pairing, or manual re-page).
 */
void bthid_br_start(struct bt_conn *conn);

/* ACL gone: release held keys, forget channel state. */
void bthid_br_stop(void);

/* True once a HID channel is up or being set up on the current ACL. */
bool bthid_br_active(void);

#endif /* BTHID_BR_H_ */
