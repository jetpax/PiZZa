/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-test shim for <zephyr/kernel.h>: just the few utility macros
 * shim_mixer.c uses, so it compiles on the host toolchain.
 */
#ifndef HOST_FAKE_ZEPHYR_KERNEL_H
#define HOST_FAKE_ZEPHYR_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define ARG_UNUSED(x) ((void)(x))

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif

#endif /* HOST_FAKE_ZEPHYR_KERNEL_H */
