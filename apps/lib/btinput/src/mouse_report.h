/*
 * apps/lib/btinput -- boot-format mouse report parser.
 *
 * Pure code: no Zephyr dependencies, so the same file compiles in the host
 * unit-test harness (tests/test_mouse_report.c).
 *
 * Input: boot mouse input reports [buttons][dx:i8][dy:i8]([wheel:i8]) --
 * 3 bytes per the HID boot protocol, 4 when the device appends the common
 * wheel extension. Button down/up events are derived by diffing the buttons
 * byte (all 8 bits -- boot defines 3, some mice report more); motion/wheel
 * pass through as signed deltas, suppressed when all-zero (idle keepalives).
 * mouse_report_release_all() synthesizes UP for held buttons (link loss).
 */

#ifndef MOUSE_REPORT_H_
#define MOUSE_REPORT_H_

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_REPORT_MIN_LEN 3

/* Button numbering follows boot-report bit order: 0=left 1=right 2=middle. */
typedef void (*mouse_btn_cb_t)(uint8_t button, bool pressed, void *user_data);
typedef void (*mouse_rel_cb_t)(int8_t dx, int8_t dy, int8_t wheel,
			       void *user_data);

struct mouse_state {
	uint8_t buttons;
};

/* Process one report; emits button edges via bcb and non-zero motion via
 * rcb. Returns false if the report was ignored (too short), true otherwise.
 */
bool mouse_report_process(struct mouse_state *st, const uint8_t *report,
			  int len, mouse_btn_cb_t bcb, mouse_rel_cb_t rcb,
			  void *user_data);

/* Emit UP for every held button and clear the state. */
void mouse_report_release_all(struct mouse_state *st, mouse_btn_cb_t bcb,
			      void *user_data);

#endif /* MOUSE_REPORT_H_ */
