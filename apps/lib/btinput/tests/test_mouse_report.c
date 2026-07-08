/*
 * apps/lib/btinput -- host-side unit tests for the boot mouse parser.
 * Pure C, no Zephyr: build with
 *   cc -Wall -o /tmp/tm test_mouse_report.c ../src/mouse_report.c && /tmp/tm
 * Vectors cover the M4_PLAN §4 list: button diff, motion, wheel, sign,
 * release-all, short-report reject -- plus idle-keepalive suppression.
 */

#include <stdio.h>
#include <string.h>

#include "../src/mouse_report.h"

/* --- event recorders -------------------------------------------------------- */

struct bev {
	uint8_t button;
	bool pressed;
};

struct rev {
	int8_t dx, dy, wheel;
};

static struct bev bevs[32];
static int n_bevs;
static struct rev revs[32];
static int n_revs;

static void brec(uint8_t button, bool pressed, void *ud)
{
	(void)ud;
	bevs[n_bevs].button = button;
	bevs[n_bevs].pressed = pressed;
	n_bevs++;
}

static void rrec(int8_t dx, int8_t dy, int8_t wheel, void *ud)
{
	(void)ud;
	revs[n_revs].dx = dx;
	revs[n_revs].dy = dy;
	revs[n_revs].wheel = wheel;
	n_revs++;
}

static void reset(struct mouse_state *st)
{
	memset(st, 0, sizeof(*st));
	n_bevs = 0;
	n_revs = 0;
}

static int find_bev(uint8_t button, bool pressed)
{
	for (int i = 0; i < n_bevs; i++) {
		if (bevs[i].button == button && bevs[i].pressed == pressed) {
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

static void test_button_press_release(void)
{
	struct mouse_state st;

	reset(&st);
	/* Left down. */
	CHECK(mouse_report_process(&st, R(0x01, 0, 0), 3, brec, rrec, NULL));
	CHECK(n_bevs == 1 && bevs[0].button == 0 && bevs[0].pressed);
	CHECK(n_revs == 0);

	/* Held (repeat report): no re-fire. */
	mouse_report_process(&st, R(0x01, 0, 0), 3, brec, rrec, NULL);
	CHECK(n_bevs == 1);

	/* Release. */
	mouse_report_process(&st, R(0x00, 0, 0), 3, brec, rrec, NULL);
	CHECK(n_bevs == 2 && bevs[1].button == 0 && !bevs[1].pressed);
	printf("ok  button_press_release\n");
}

static void test_button_diff_multi(void)
{
	struct mouse_state st;

	reset(&st);
	/* Left+right down together. */
	mouse_report_process(&st, R(0x03, 0, 0), 3, brec, rrec, NULL);
	CHECK(n_bevs == 2);
	CHECK(find_bev(0, true) >= 0 && find_bev(1, true) >= 0);

	/* Swap to middle only: two UPs + one DOWN in one report. */
	mouse_report_process(&st, R(0x04, 0, 0), 3, brec, rrec, NULL);
	CHECK(n_bevs == 5);
	CHECK(find_bev(0, false) >= 0 && find_bev(1, false) >= 0 &&
	      find_bev(2, true) >= 0);
	printf("ok  button_diff_multi\n");
}

static void test_motion_sign(void)
{
	struct mouse_state st;

	reset(&st);
	/* dx=-1 (0xff), dy=+127. */
	mouse_report_process(&st, R(0x00, 0xff, 0x7f), 3, brec, rrec, NULL);
	CHECK(n_revs == 1 && revs[0].dx == -1 && revs[0].dy == 127 &&
	      revs[0].wheel == 0);

	/* dx=-128 (0x80), dy=-2 (0xfe). */
	mouse_report_process(&st, R(0x00, 0x80, 0xfe), 3, brec, rrec, NULL);
	CHECK(n_revs == 2 && revs[1].dx == -128 && revs[1].dy == -2);
	CHECK(n_bevs == 0);
	printf("ok  motion_sign\n");
}

static void test_wheel(void)
{
	struct mouse_state st;

	reset(&st);
	/* 4-byte report, wheel = -1. */
	mouse_report_process(&st, R(0x00, 0, 0, 0xff), 4, brec, rrec, NULL);
	CHECK(n_revs == 1 && revs[0].wheel == -1 && revs[0].dx == 0);

	/* 3-byte report has no wheel: motion only. */
	mouse_report_process(&st, R(0x00, 5, 0), 3, brec, rrec, NULL);
	CHECK(n_revs == 2 && revs[1].wheel == 0 && revs[1].dx == 5);
	printf("ok  wheel\n");
}

static void test_idle_keepalive_quiet(void)
{
	struct mouse_state st;

	reset(&st);
	/* All-zero keepalives: no button edges, no motion callbacks. */
	for (int i = 0; i < 3; i++) {
		CHECK(mouse_report_process(&st, R(0x00, 0, 0, 0), 4, brec, rrec, NULL));
	}
	CHECK(n_bevs == 0 && n_revs == 0);
	printf("ok  idle_keepalive_quiet\n");
}

static void test_motion_while_held(void)
{
	struct mouse_state st;

	reset(&st);
	/* Drag: button down + motion in one report. */
	mouse_report_process(&st, R(0x01, 3, 0xfc), 3, brec, rrec, NULL);
	CHECK(n_bevs == 1 && bevs[0].pressed);
	CHECK(n_revs == 1 && revs[0].dx == 3 && revs[0].dy == -4);

	/* Continued drag: motion only, held button quiet. */
	mouse_report_process(&st, R(0x01, 1, 1), 3, brec, rrec, NULL);
	CHECK(n_bevs == 1 && n_revs == 2);
	printf("ok  motion_while_held\n");
}

static void test_release_all(void)
{
	struct mouse_state st;

	reset(&st);
	mouse_report_process(&st, R(0x05, 0, 0), 3, brec, rrec, NULL);
	n_bevs = 0;

	mouse_report_release_all(&st, brec, NULL);
	CHECK(n_bevs == 2);
	CHECK(find_bev(0, false) >= 0 && find_bev(2, false) >= 0);

	/* Idempotent. */
	mouse_report_release_all(&st, brec, NULL);
	CHECK(n_bevs == 2);
	printf("ok  release_all\n");
}

static void test_short_report_rejected(void)
{
	struct mouse_state st;

	reset(&st);
	CHECK(!mouse_report_process(&st, R(0x01, 5), 2, brec, rrec, NULL));
	CHECK(n_bevs == 0 && n_revs == 0);
	printf("ok  short_report_rejected\n");
}

int main(void)
{
	test_button_press_release();
	test_button_diff_multi();
	test_motion_sign();
	test_wheel();
	test_idle_keepalive_quiet();
	test_motion_while_held();
	test_release_all();
	test_short_report_rejected();

	if (failures) {
		printf("\n%d FAILURE(S)\n", failures);
		return 1;
	}
	printf("\nall tests passed\n");
	return 0;
}
