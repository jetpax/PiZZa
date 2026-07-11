/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- Zephyr/host compatibility for the dual-target
 * shim TUs (surface, render, ttf-atlas, image-pimg). Those files are
 * host-testable like src/audio/: on Zephyr this header is a thin pass
 * to the kernel/log headers; on the host (tests/) it supplies the
 * handful of Zephyr idioms they use.
 */

#ifndef PIZZA_S2S_COMPAT_H
#define PIZZA_S2S_COMPAT_H

#include <stdbool.h>

#ifdef __ZEPHYR__

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#define S2S_LOG_REGISTER(mod) LOG_MODULE_REGISTER(mod, CONFIG_SDL2SHIM_LOG_LEVEL)

#else /* host test build */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define S2S_LOG_REGISTER(mod) struct s2s_compat_semi##mod
#define LOG_DBG(...) do { } while (0)
#define LOG_INF(...) do { fprintf(stderr, "inf: " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOG_WRN(...) do { fprintf(stderr, "wrn: " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOG_ERR(...) do { fprintf(stderr, "err: " __VA_ARGS__); fputc('\n', stderr); } while (0)

#define ARG_UNUSED(x) (void)(x)

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

/* Hosts running the tests (Apple Silicon / x86_64) are little-endian,
 * same as every target this shim serves.
 */
#define sys_le16_to_cpu(x) (x)
#define sys_le32_to_cpu(x) (x)

#endif /* __ZEPHYR__ */

#endif /* PIZZA_S2S_COMPAT_H */
