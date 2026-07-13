/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa libretro frontend -- public entry point.
 */

#ifndef PIZZA_RETRO_FRONTEND_H
#define PIZZA_RETRO_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>

/* Bind callbacks, init the core, load no-content, then pace
 * retro_run() at the core's declared fps. Returns 0 on a clean
 * teardown (return-to-menu / cycle test), negative on a bind/load
 * failure. In single-core forever mode it does not return.
 *
 * This is the open() + run_loaded(NULL) + close() sequence below rolled
 * into one call, for the single-core appliance (no launcher menu).
 */
int retro_frontend_run(void);

/* Split lifecycle for the launcher: the core is bound and its content
 * requirements known BEFORE a game is loaded, so the menu can browse
 * content (and, later, present options) in between.
 *
 *   open()          -- bind the core, wire callbacks, init(), read
 *                      system info. 0 on success; the core stays bound.
 *   content_req()   -- what the just-opened core wants (valid only
 *                      between open() and close()).
 *   run_loaded(p)   -- load_game(p) (p == NULL: no content) and pace the
 *                      run loop until return-to-menu. 0 clean, <0 on a
 *                      load failure.
 *   close()         -- unload_game + deinit + unbind. Safe after a
 *                      failed run_loaded (partial teardown).
 */
struct retro_content_req {
	bool wants_content;           /* core cannot run without content */
	bool need_fullpath;           /* content passed by path, not data */
	const char *valid_extensions; /* pipe list, e.g. "gb|gbc"; may be NULL */
	const char *library_name;     /* core display name; may be NULL */
};

int retro_frontend_open(void);
const struct retro_content_req *retro_frontend_content_req(void);
int retro_frontend_run_loaded(const char *content_path);
void retro_frontend_close(void);

#ifdef CONFIG_RETRO_MENU
/* Ask the running core to tear down and return to the launcher (the
 * `retro menu` shell command). Also reachable via the L+R pad chord.
 */
void retro_request_menu(void);
#endif

#endif /* PIZZA_RETRO_FRONTEND_H */
