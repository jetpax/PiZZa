/*
 * apps/lib/btinput -- internals shared between the BR and LE backends.
 *
 * The consumer-facing sinks (key / mouse button / mouse move) are registered
 * once via btinput.h and stored in hid_host_br.c; both backends feed them
 * through these trampolines, which match the parser callback signatures.
 */

#ifndef BTINPUT_PRIV_H_
#define BTINPUT_PRIV_H_

#include <stdint.h>
#include <stdbool.h>

void btinput_key_trampoline(uint8_t usage, bool pressed, void *user_data);
void btinput_mouse_btn_trampoline(uint8_t button, bool pressed,
				  void *user_data);
void btinput_mouse_move_trampoline(int8_t dx, int8_t dy, int8_t wheel,
				   void *user_data);

/* Controller-wedge guard (CONFIG_BTINPUT_WEDGE_REBOOT, wedge_guard.c):
 * bracket a risky HCI sync command (the rescue page) so a fatal
 * controller-unresponsive assert inside it self-heals with a clean
 * cold reboot instead of hanging the appliance. No-ops when disabled.
 */
#ifdef CONFIG_BTINPUT_WEDGE_REBOOT
void btinput_wedge_arm(void);
void btinput_wedge_disarm(void);
#else
static inline void btinput_wedge_arm(void) {}
static inline void btinput_wedge_disarm(void) {}
#endif

#endif /* BTINPUT_PRIV_H_ */
