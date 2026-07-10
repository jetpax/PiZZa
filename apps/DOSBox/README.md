<!-- SPDX-License-Identifier: Apache-2.0 -->
# DOSBox-X on PiZZa

A DOS PC on Zephyr / BCM2710. HDMI video, HDMI audio via VC4 MAI on the
same cable, keyboard via the `dosbox` USB CDC shell or a paired BT HID
device through [`apps/lib/btinput`](../lib/btinput). DOS/4GW binaries
work; DOOM runs at ~16 fps at 1 GHz. Runs on the Raspberry Pi Zero 2 W.

## Prerequisites

```sh
git clone https://github.com/joncampbell123/dosbox-x ~/github/dosbox-x
```

Override with `-DDBX=<path>` if yours lives elsewhere.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
```

Hardware (`rpi_zero_2w`, Claude builds, user flashes):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/DOSBox -d build-dosbox-hw \
  -- -DTOOLCHAIN_HAS_GLIBCXX=ON
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-dosbox-hw/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

Sim (`qemu_cortex_a53`):

```sh
west build -p always -b qemu_cortex_a53 \
  -s ~/github/SS/PiZZa/apps/DOSBox -d build-dosbox-qemu \
  -- -DTOOLCHAIN_HAS_GLIBCXX=ON
west build -d build-dosbox-qemu -t run
```

## Running

HDMI shows DOS. Connect to the `dosbox` shell on the USB CDC ACM port
(`/dev/cu.usbmodem*`) or pair a Bluetooth keyboard; boot logs are on the
mini-UART, GPIO 14/15 at 115200.

See [`NOTES.md`](NOTES.md) for the assets pipeline, layout, and
licensing.
