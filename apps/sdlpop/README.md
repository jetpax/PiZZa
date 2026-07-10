<!-- SPDX-License-Identifier: Apache-2.0 -->
# SDLPoP on PiZZa

The open-source Prince of Persia remake
([NagyD/SDLPoP](https://github.com/NagyD/SDLPoP), pure C) on Zephyr /
BCM2710. HDMI video via [`apps/lib/sdl2shim`](../lib/sdl2shim) and the
BCM2835 framebuffer with VC4 HVS scaling; SFX + music out of HDMI on the
same cable. Keyboard over the `pop` USB CDC shell. Runs on the Raspberry
Pi Zero 2 W.

## Prerequisites

```sh
git clone https://github.com/NagyD/SDLPoP ~/github/SDLPoP
```

Override with `-DPOP_SRC_DIR=<path>` if yours lives elsewhere. The
`data/` tree next to it is packed automatically.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
```

Hardware (`rpi_zero_2w`, Claude builds, user flashes):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/SDLPoP -d build-sdlpop
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-sdlpop/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

Sim (`qemu_cortex_a53`):

```sh
west build -p always -b qemu_cortex_a53 \
  -s ~/github/SS/PiZZa/apps/SDLPoP -d build-sdlpop-qemu
west build -d build-sdlpop-qemu -t run
```

## Running

HDMI shows the game and carries the audio. Connect to the `pop` shell on
the USB CDC ACM port (`/dev/cu.usbmodem*`) to inject keys; boot logs are
on the mini-UART, GPIO 14/15 at 115200.

See [`NOTES.md`](NOTES.md) for the shell key reference, the asset
pipeline, the audio backend, layout, and licensing.
