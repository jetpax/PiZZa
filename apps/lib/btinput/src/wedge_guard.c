/*
 * apps/lib/btinput -- controller-wedge guard (CONFIG_BTINPUT_WEDGE_REBOOT).
 *
 * The AIROC 43430 can raise a hardware error and go unresponsive; the
 * next HCI sync command then times out and the Zephyr host asserts
 * (BT_ASSERT_MSG "Controller unresponsive", hci_core.c) -- fatal, and
 * the host cannot cleanly continue (the timed-out command buffer would
 * be used after free). On an appliance that means a hung device.
 *
 * The manager brackets its rescue page (the observed wedge point) with
 * btinput_wedge_arm()/disarm(). If a fatal fires while armed, this
 * handler self-heals with a clean cold reboot instead of halting; every
 * other fault gets the default halt. Because arm() only happens after
 * BT init + a disconnect, an init-time fault still halts -- no reboot
 * loop. The rescue page also runs only when the pad is already
 * disconnected, so a recovery reboot never interrupts active play.
 */

#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>

#include "btinput_priv.h"

LOG_MODULE_REGISTER(btinput_wedge, CONFIG_BTINPUT_LOG_LEVEL);

static volatile bool armed;

void btinput_wedge_arm(void)
{
	armed = true;
}

void btinput_wedge_disarm(void)
{
	armed = false;
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();

	if (armed) {
		printk("btinput: BT controller wedged (fatal %u) -- "
		       "cold reboot to recover\n", reason);
		sys_reboot(SYS_REBOOT_COLD);
		/* reboot does not return */
	}

	printk("Halting system\n");
	k_fatal_halt(reason);
	CODE_UNREACHABLE;
}
