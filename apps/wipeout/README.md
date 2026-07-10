<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# WipEout on PiZZa

The 1995 WipEout on Zephyr / BCM2710 with a 4-core SMP renderer. 320x240
via the BCM2835 framebuffer, VC4 HVS scales to the monitor, ~30 fps at
1 GHz on hardware. Runs on the Raspberry Pi Zero 2 W.

## Prerequisites

```sh
git clone https://github.com/phoboslab/wipeout-rewrite ~/github/wipeout-rewrite
```

Override with `-DWIPEOUT_SRC_DIR=<path>` if yours lives elsewhere. The
SMP wiring needs the `dev` branch of the [jetpax Zephyr
fork](https://github.com/jetpax/zephyr); see [`NOTES.md`](NOTES.md).

Copy the ~150 MB WipEout data (`wipeout-data-v01.zip` from the upstream
release) onto an SD card FAT partition mounted as `/DATA:` at
`/wipeout/`. The game reads them at runtime; they are not baked into
the binary.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-

cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/Wipeout -d build-wipeout
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-wipeout/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

## Running

HDMI shows the game. The `wipeout` shell on USB CDC ACM
(`/dev/cu.usbmodem*`) carries diagnostics; boot logs are on the
mini-UART at 115200.

See [`NOTES.md`](NOTES.md) for the Zephyr fork setup, shell commands,
renderer options, layout, and licensing.
