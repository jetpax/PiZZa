#include <zephyr/llext/symbol.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	printk("\n=== Hello from a PiZZA sketch! ===\n");
	printk("(linked at boot from /SD:/sketch.llext via fs_loader)\n");
	return 0;
}
LL_EXTENSION_SYMBOL(main);
