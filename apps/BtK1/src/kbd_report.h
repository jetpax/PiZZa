/*
 * WO-BT-K1 M3 (T3.3) — boot-format keyboard report diff parser.
 *
 * Pure code: no Zephyr dependencies, so the same file compiles in the
 * native_sim / host unit-test harness (T3.4).
 *
 * Input: 8-byte boot keyboard input reports (modifiers, reserved,
 * 6 keycodes). Key down/up events are derived by diffing consecutive
 * reports; the phantom/rollover report (keycodes all 0x01) is ignored;
 * kbd_report_release_all() synthesizes UP events for everything held
 * (link loss / pad sleep, T4.2).
 */

#ifndef KBD_REPORT_H_
#define KBD_REPORT_H_

#include <stdint.h>
#include <stdbool.h>

#define KBD_REPORT_LEN   8
#define KBD_MAX_KEYS     6

/* Modifier bits are reported as HID usages 0xE0..0xE7 through the same
 * callback as normal keys.
 */
#define KBD_USAGE_MOD_BASE 0xE0

typedef void (*kbd_event_cb_t)(uint8_t usage, bool pressed, void *user_data);

struct kbd_state {
	uint8_t mods;
	uint8_t keys[KBD_MAX_KEYS];
};

/* Process one report; emits down/up events via cb. Returns false if the
 * report was ignored (wrong length or phantom/rollover), true otherwise.
 */
bool kbd_report_process(struct kbd_state *st, const uint8_t *report, int len,
			kbd_event_cb_t cb, void *user_data);

/* Emit UP for every held key/modifier and clear the state. */
void kbd_report_release_all(struct kbd_state *st, kbd_event_cb_t cb,
			    void *user_data);

#endif /* KBD_REPORT_H_ */
