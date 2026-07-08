/*
 * apps/lib/btinput -- host-side unit tests for the boot-report diff parser.
 * Pure C, no Zephyr: build with
 *   cc -Wall -o /tmp/t test_kbd_report.c ../src/kbd_report.c && /tmp/t
 * Vectors mirror behaviors observed on HW 2026-07-08 (triple-sent reports,
 * chords, release-on-disconnect) plus the spec edge cases (phantom rollover,
 * modifiers, short reports).
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "../src/kbd_report.h"

/* --- event recorder -------------------------------------------------------- */

struct ev {
	uint8_t usage;
	bool pressed;
};

static struct ev evs[64];
static int n_evs;

static void rec(uint8_t usage, bool pressed, void *user_data)
{
	(void)user_data;
	evs[n_evs].usage = usage;
	evs[n_evs].pressed = pressed;
	n_evs++;
}

static void reset(struct kbd_state *st)
{
	memset(st, 0, sizeof(*st));
	n_evs = 0;
}

static int find_ev(uint8_t usage, bool pressed)
{
	for (int i = 0; i < n_evs; i++) {
		if (evs[i].usage == usage && evs[i].pressed == pressed) {
			return i;
		}
	}
	return -1;
}

#define R(...) ((const uint8_t[]){ __VA_ARGS__ })

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* --- tests ----------------------------------------------------------------- */

static void test_single_press_release(void)
{
	struct kbd_state st;

	reset(&st);
	CHECK(kbd_report_process(&st, R(0, 0, 0x0a, 0, 0, 0, 0, 0), 8, rec, NULL));
	CHECK(n_evs == 1 && evs[0].usage == 0x0a && evs[0].pressed);

	CHECK(kbd_report_process(&st, R(0, 0, 0, 0, 0, 0, 0, 0), 8, rec, NULL));
	CHECK(n_evs == 2 && evs[1].usage == 0x0a && !evs[1].pressed);
	printf("ok  single_press_release\n");
}

static void test_repeat_dedup(void)
{
	struct kbd_state st;

	reset(&st);
	/* The 8BitDo triple-sends every report. */
	for (int i = 0; i < 3; i++) {
		kbd_report_process(&st, R(0, 0, 0x16, 0, 0, 0, 0, 0), 8, rec, NULL);
	}
	CHECK(n_evs == 1);
	for (int i = 0; i < 3; i++) {
		kbd_report_process(&st, R(0, 0, 0, 0, 0, 0, 0, 0), 8, rec, NULL);
	}
	CHECK(n_evs == 2);
	printf("ok  repeat_dedup\n");
}

static void test_chord(void)
{
	struct kbd_state st;

	reset(&st);
	/* A+B land in one report (observed on HW). */
	kbd_report_process(&st, R(0, 0, 0x0a, 0x0d, 0, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 2);
	CHECK(find_ev(0x0a, true) >= 0 && find_ev(0x0d, true) >= 0);

	/* +X */
	kbd_report_process(&st, R(0, 0, 0x0a, 0x0d, 0x0b, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 3 && find_ev(0x0b, true) >= 0);

	/* release X only */
	kbd_report_process(&st, R(0, 0, 0x0a, 0x0d, 0, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 4 && find_ev(0x0b, false) >= 0);

	/* release the rest */
	kbd_report_process(&st, R(0, 0, 0, 0, 0, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 6);
	CHECK(find_ev(0x0a, false) >= 0 && find_ev(0x0d, false) >= 0);

	/* slot order must not matter */
	reset(&st);
	kbd_report_process(&st, R(0, 0, 0x04, 0x05, 0, 0, 0, 0), 8, rec, NULL);
	n_evs = 0;
	kbd_report_process(&st, R(0, 0, 0x05, 0x04, 0, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 0);
	printf("ok  chord\n");
}

static void test_phantom_rollover(void)
{
	struct kbd_state st;

	reset(&st);
	kbd_report_process(&st, R(0, 0, 0x0a, 0x0d, 0, 0, 0, 0), 8, rec, NULL);
	n_evs = 0;

	/* ErrorRollOver: all slots 0x01 -- must be ignored, state kept. */
	CHECK(!kbd_report_process(&st, R(0, 0, 1, 1, 1, 1, 1, 1), 8, rec, NULL));
	CHECK(n_evs == 0);

	/* Recovery report: A still held, B gone -> exactly one UP. */
	kbd_report_process(&st, R(0, 0, 0x0a, 0, 0, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 1 && evs[0].usage == 0x0d && !evs[0].pressed);
	printf("ok  phantom_rollover\n");
}

static void test_modifiers(void)
{
	struct kbd_state st;

	reset(&st);
	/* LSHIFT (bit1) + LCTRL (bit0) down together. */
	kbd_report_process(&st, R(0x03, 0, 0, 0, 0, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 2);
	CHECK(find_ev(0xe0, true) >= 0 && find_ev(0xe1, true) >= 0);

	/* Drop LCTRL, add RALT (bit6). */
	kbd_report_process(&st, R(0x42, 0, 0, 0, 0, 0, 0, 0), 8, rec, NULL);
	CHECK(n_evs == 4);
	CHECK(find_ev(0xe0, false) >= 0 && find_ev(0xe6, true) >= 0);
	printf("ok  modifiers\n");
}

static void test_release_all(void)
{
	struct kbd_state st;

	reset(&st);
	/* Held key + modifier, then link loss (observed live on HW: pad
	 * powered off while 0x16 held).
	 */
	kbd_report_process(&st, R(0x02, 0, 0x16, 0x04, 0, 0, 0, 0), 8, rec, NULL);
	n_evs = 0;

	kbd_report_release_all(&st, rec, NULL);
	CHECK(n_evs == 3);
	CHECK(find_ev(0xe1, false) >= 0);
	CHECK(find_ev(0x16, false) >= 0 && find_ev(0x04, false) >= 0);

	/* Idempotent: nothing held now. */
	kbd_report_release_all(&st, rec, NULL);
	CHECK(n_evs == 3);
	printf("ok  release_all\n");
}

static void test_short_report_rejected(void)
{
	struct kbd_state st;

	reset(&st);
	CHECK(!kbd_report_process(&st, R(0, 0, 0x0a, 0, 0, 0, 0), 7, rec, NULL));
	CHECK(n_evs == 0);
	printf("ok  short_report_rejected\n");
}

int main(void)
{
	test_single_press_release();
	test_repeat_dedup();
	test_chord();
	test_phantom_rollover();
	test_modifiers();
	test_release_all();
	test_short_report_rejected();

	if (failures) {
		printf("\n%d FAILURE(S)\n", failures);
		return 1;
	}
	printf("\nall tests passed\n");
	return 0;
}
