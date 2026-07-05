/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDLPoP port -- USB device stack bring-up (CDC ACM shell).
 *
 * The board conf sets CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n
 * (required on BCM283x -- auto-init races the firmware-mediated USB
 * power-on), so the app must enable USBD itself. Same proven
 * sequence as the wipeout / PizzaShell ports.
 *
 * Brought up at APPLICATION init, NOT from main(): the game's
 * asset-pack setup and first render add enough latency that
 * enabling USBD from main() can miss the host's (macOS) USB
 * enumeration retry window, silently killing CDC until a replug.
 * APPLICATION init captures the window early; main() then runs while
 * the host finishes descriptor exchange in parallel.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT)

#include <zephyr/usb/usbd.h>
#include <sample_usbd.h>

static int pop_usb_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(NULL);

	if (usbd == NULL) {
		printk("[sdlpop] sample_usbd_init_device failed\n");
		return -ENODEV;
	}
	if (!usbd_can_detect_vbus(usbd)) {
		int err = usbd_enable(usbd);

		if (err) {
			printk("[sdlpop] usbd_enable failed: %d\n", err);
			return err;
		}
	}
	return 0;
}

SYS_INIT(pop_usb_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_USB_DEVICE_STACK_NEXT */
