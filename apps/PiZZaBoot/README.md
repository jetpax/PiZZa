<!-- SPDX-License-Identifier: Apache-2.0 -->
# PiZZaBoot

The PiZZa boot menu: a Zephyr app whose only job is to pick which other
Zephyr image the Pi boots. It draws the same list on HDMI, the mini-UART
and the USB-CDC console at once, and it is what a boot-menu card
(`make-sdcard.sh --menu`, `pizza-menu-*.img.xz`) boots by default.

The GPU firmware is the real selector. `config.txt` defaults to
`kernel=bootmenu.bin` and then `include chosen.txt`, so once you pick an
entry PiZZaBoot writes `chosen.txt` and resets — every later boot goes
straight into that image with no menu and no added boot time. Holding a
button between **GPIO 17** and GND at power-on takes the
`[gpio17=0] kernel=bootmenu.bin` branch and brings the menu back, which
also rescues a corrupt `chosen.txt`.

Entries come from `menu.txt` on the boot FAT:

```
timeout = 5
default = RetroPiZZa

RetroPiZZa = retropizza.bin
PiZZa Shell = pizzashell.bin
```

Missing files are listed but greyed out; with no `menu.txt` at all, every
`*.bin` at the FAT root is offered instead. The countdown to `default`
runs only when there is no valid `chosen.txt`, so a fresh card always
opens on the menu. Navigate with the arrow keys and Enter, a digit key,
or the GPIO 17 button — short press cycles, long press boots.

Shell-capable images reach the same mechanism through
[`apps/lib/bootsel`](../lib/bootsel): `boot list`, `boot <name>`,
`boot menu`.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-

cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/PiZZaBoot -d build-pizzaboot
```

Then stage it as a card's menu, together with one kernel per entry:

```sh
cd ~/github/SS/PiZZa
./make-sdcard.sh rpi_zero_2w --menu ~/zephyrproject/build-pizzaboot/zephyr/zephyr.bin \
  "RetroPiZZa=<retro zephyr.bin>" \
  "PiZZa Shell=<shell zephyr.bin>" -o pizza-menu.img
```

To replace just the menu on an existing card, `install-to-sdcard.sh`
takes `--slot`; `bootmenu.bin` itself is not a slot, so copy it over
directly.
