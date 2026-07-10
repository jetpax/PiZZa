<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Faux86 on PiZZa

An XT-class PC (8086, CGA/EGA/VGA) on Zephyr / BCM2710. Boots to the
`A:\>` MS-DOS prompt on the HDMI panel. Runs on the Raspberry Pi Zero 2 W.

## Prerequisites

```sh
git clone https://github.com/ArnoldUK/Faux86-remake ~/github/Faux86-remake
```

Override with `-DFAUX86=<path>` if yours lives elsewhere.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
```

Hardware (`rpi_zero_2w`, Claude builds, user flashes):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/faux86 -d build-faux86 \
  -- -DTOOLCHAIN_HAS_GLIBCXX=ON
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-faux86/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

Sim (`qemu_cortex_a53`):

```sh
west build -p always -b qemu_cortex_a53 \
  -s ~/github/SS/PiZZa/apps/faux86 -d build-faux86-qemu \
  -- -DTOOLCHAIN_HAS_GLIBCXX=ON
west build -d build-faux86-qemu -t run
```

## Running

HDMI shows the XT BIOS POST and the DOS boot. Logs are on the mini-UART,
GPIO 14/15 at 115200. Input is not wired yet.

See [`NOTES.md`](NOTES.md) for architecture, assets, layout, and licensing.
