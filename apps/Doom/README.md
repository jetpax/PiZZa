<!-- SPDX-License-Identifier: Apache-2.0 -->
# Doom on PiZZa

The 1993 Doom on Zephyr / BCM2710. Native 320x200 to a BCM2835
framebuffer, HVS scales to the monitor, SFX and OPL FM music out of HDMI
on the same cable. Keyboard via the `doom` USB CDC shell or a paired BT
HID keyboard through [`apps/lib/btinput`](../lib/btinput). Runs on the
Raspberry Pi Zero 2 W.

## Prerequisites

```sh
git clone https://github.com/AlexOberhofer/sdl2-doom ~/github/sdl2-doom
```

For OPL music, also clone Chocolate Doom (the OPL sequencer + Nuked OPL3
emulator):

```sh
git clone https://github.com/chocolate-doom/chocolate-doom ~/github/chocolate-doom
```

Override with `-DDOOM_SRC_DIR=<path>` and `-DCHOCOLATE_SRC_DIR=<path>` if
yours live elsewhere. Drop a `DOOM1.WAD` (the shareware IWAD) into
`assets/` for the packed asset build.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
```

Hardware (`rpi_zero_2w`, Claude builds, user flashes):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/Doom -d build-doom-hw
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-doom-hw/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

Sim (`qemu_cortex_a53`):

```sh
west build -p always -b qemu_cortex_a53 \
  -s ~/github/SS/PiZZa/apps/Doom -d build-doom-qemu
west build -d build-doom-qemu -t run
```

## Running

HDMI shows the game and carries audio. Connect to the `doom` shell on
the USB CDC ACM port (`/dev/cu.usbmodem*`) to inject keys; boot logs are
on the mini-UART, GPIO 14/15 at 115200.

See [`NOTES.md`](NOTES.md) for the shell command reference, OPL music
config, sim tuning, layout, and licensing.
