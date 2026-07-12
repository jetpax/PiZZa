/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- public entry point.
 */

#ifndef PIZZA_RETRO_FRONTEND_H
#define PIZZA_RETRO_FRONTEND_H

/* Bind callbacks, init the core, load no-content, then pace
 * retro_run() at the core's declared fps. Returns 0 on a clean
 * teardown (return-to-menu / cycle test), negative on a bind/load
 * failure. In single-core forever mode it does not return.
 */
int retro_frontend_run(void);

#ifdef CONFIG_RETRO_MENU
/* Ask the running core to tear down and return to the launcher (the
 * `retro menu` shell command). Also reachable via the L+R pad chord.
 */
void retro_request_menu(void);
#endif

#endif /* PIZZA_RETRO_FRONTEND_H */
