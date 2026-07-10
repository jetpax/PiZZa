#include <string.h>

#include "mouse_report.h"

bool mouse_report_process(struct mouse_state *st, const uint8_t *report,
			  int len, mouse_btn_cb_t bcb, mouse_rel_cb_t rcb,
			  void *user_data)
{
	uint8_t buttons;
	int8_t dx, dy, wheel;

	if (len < MOUSE_REPORT_MIN_LEN) {
		return false;
	}

	buttons = report[0];
	dx = (int8_t)report[1];
	dy = (int8_t)report[2];
	wheel = (len >= 4) ? (int8_t)report[3] : 0;

	/* Button diff: one event per toggled bit. */
	for (int bit = 0; bit < 8; bit++) {
		uint8_t mask = 1u << bit;

		if ((buttons ^ st->buttons) & mask) {
			bcb(bit, (buttons & mask) != 0, user_data);
		}
	}
	st->buttons = buttons;

	/* Motion/wheel: pass through, but stay quiet on the all-zero idle
	 * keepalives HID devices stream.
	 */
	if ((dx != 0 || dy != 0 || wheel != 0) && rcb != NULL) {
		rcb(dx, dy, wheel, user_data);
	}
	return true;
}

void mouse_report_release_all(struct mouse_state *st, mouse_btn_cb_t bcb,
			      void *user_data)
{
	for (int bit = 0; bit < 8; bit++) {
		if (st->buttons & (1u << bit)) {
			bcb(bit, false, user_data);
		}
	}
	memset(st, 0, sizeof(*st));
}
