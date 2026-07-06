/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-test shim for <zephyr/logging/log.h>: LOG_* become no-ops.
 */
#ifndef HOST_FAKE_ZEPHYR_LOG_H
#define HOST_FAKE_ZEPHYR_LOG_H

#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_ERR(...)
#define LOG_DBG(...)

#endif /* HOST_FAKE_ZEPHYR_LOG_H */
