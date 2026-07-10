#include <string.h>

#include "kbd_report.h"

static bool key_in(const uint8_t *keys, uint8_t usage)
{
	for (int i = 0; i < KBD_MAX_KEYS; i++) {
		if (keys[i] == usage) {
			return true;
		}
	}
	return false;
}

static bool is_phantom(const uint8_t *keys)
{
	/* ErrorRollOver: the device fills every slot with 0x01. */
	for (int i = 0; i < KBD_MAX_KEYS; i++) {
		if (keys[i] != 0x01) {
			return false;
		}
	}
	return true;
}

bool kbd_report_process(struct kbd_state *st, const uint8_t *report, int len,
			kbd_event_cb_t cb, void *user_data)
{
	const uint8_t *keys;
	uint8_t mods;

	if (len < KBD_REPORT_LEN) {
		return false;
	}
	mods = report[0];
	keys = &report[2];

	if (is_phantom(keys)) {
		/* Rollover: state unknown; keep prior state, drop report. */
		return false;
	}

	/* Modifier diff: one event per toggled bit. */
	for (int bit = 0; bit < 8; bit++) {
		uint8_t mask = 1u << bit;

		if ((mods ^ st->mods) & mask) {
			cb(KBD_USAGE_MOD_BASE + bit, (mods & mask) != 0, user_data);
		}
	}
	st->mods = mods;

	/* Releases: in old, not in new. */
	for (int i = 0; i < KBD_MAX_KEYS; i++) {
		uint8_t u = st->keys[i];

		if (u > 0x01 && !key_in(keys, u)) {
			cb(u, false, user_data);
		}
	}

	/* Presses: in new, not in old. */
	for (int i = 0; i < KBD_MAX_KEYS; i++) {
		uint8_t u = keys[i];

		if (u > 0x01 && !key_in(st->keys, u)) {
			cb(u, true, user_data);
		}
	}

	memcpy(st->keys, keys, KBD_MAX_KEYS);
	return true;
}

void kbd_report_release_all(struct kbd_state *st, kbd_event_cb_t cb,
			    void *user_data)
{
	for (int bit = 0; bit < 8; bit++) {
		if (st->mods & (1u << bit)) {
			cb(KBD_USAGE_MOD_BASE + bit, false, user_data);
		}
	}
	for (int i = 0; i < KBD_MAX_KEYS; i++) {
		if (st->keys[i] > 0x01) {
			cb(st->keys[i], false, user_data);
		}
	}
	memset(st, 0, sizeof(*st));
}
