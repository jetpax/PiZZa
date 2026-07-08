#pragma once
/* Zephyr/desktop shim for Circle's <circle/util.h> -- global mem/str fns.
 * <strings.h> for strcasecmp (Config.h maps strcmpi->strcasecmp on non-win32;
 * picolibc does not pull it in transitively like the Mac/SDL headers did). */
#include <string.h>
#include <strings.h>
#include <stdlib.h>
