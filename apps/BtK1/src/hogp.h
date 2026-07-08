/*
 * WO-BT-K1 M3 — HOGP client: HIDS discovery + input-report subscription.
 */

#ifndef HOGP_H_
#define HOGP_H_

#include <zephyr/bluetooth/conn.h>

/* Kick off HIDS discovery + subscription on an encrypted connection. */
void hogp_start(struct bt_conn *conn);

/* Link gone: release held keys, forget discovered handles. */
void hogp_stop(void);

#endif /* HOGP_H_ */
