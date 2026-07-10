<!-- SPDX-License-Identifier: Apache-2.0 -->
# PizzaShell

The default PiZZa image: an interactive shell over USB CDC ACM (Zero 2 W)
or the mini-UART (Zero W), with a `pizza about` menu that reports SoC,
memory, storage, HDMI, Wi-Fi and die temperature. One source, both boards.
Contents of a stock `pizza-shell-*.img.xz` release.

The [top-level README](../../README.md) covers the flash / first-boot
walk-through for a released image; the steps below are for rebuilding
the app.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
```

Raspberry Pi Zero 2 W (Claude builds, user flashes):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/PizzaShell -d build-pizzashell-z2w
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-pizzashell-z2w/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

Original Pi Zero W (32-bit ARMv6):

```sh
export CROSS_COMPILE=$HOME/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-
west build -p always -b rpi_zero_w \
  -s ~/github/SS/PiZZa/apps/PizzaShell -d build-pizzashell-zw
./install-to-sdcard.sh rpi_zero_w ~/zephyrproject/build-pizzashell-zw/zephyr/zephyr.bin
```

To roll a full flashable image instead of updating an existing card:

```sh
./make-sdcard.sh rpi_zero_2w ~/zephyrproject/build-pizzashell-z2w/zephyr/zephyr.bin
```

## Running

Plug the Pi's inner USB port (`USB`, not `PWR`) into the host. On the
Zero 2 W a CDC serial device enumerates (`/dev/cu.usbmodem*` on macOS,
`/dev/ttyACM*` on Linux). On the Zero W, wire GPIO 14/15 at 115200 with
a USB-serial adapter.

See [`NOTES.md`](NOTES.md) for the shell command reference and layout.
