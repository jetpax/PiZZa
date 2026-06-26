/* SPDX-License-Identifier: Apache-2.0
 *
 * Forever-loop test sketch for the CDC-loader runtime spike. Mimics an
 * Arduino setup()/loop() that never returns -- prints a versioned tick
 * once a second so we can see which sketch is running and prove an
 * in-process swap to a different one.
 */
#include <zephyr/llext/symbol.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>

#ifndef TICK_VERSION
#define TICK_VERSION "v1"
#endif

int main(void)
{
	int i = 0;

	printk("[sketch %s] setup()\n", TICK_VERSION);
	for (;;) {
		printk("[sketch %s] tick %d\n", TICK_VERSION, i++);
		k_msleep(1000);
	}
	return 0;
}
LL_EXTENSION_SYMBOL(main);
