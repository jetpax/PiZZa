/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- public entry point.
 */

#ifndef PIZZA_RETRO_FRONTEND_H
#define PIZZA_RETRO_FRONTEND_H

/* Bind callbacks, init the core, load no-content, then pace
 * retro_run() at the core's declared fps. Only returns on a load
 * failure.
 */
int retro_frontend_run(void);

#endif /* PIZZA_RETRO_FRONTEND_H */
