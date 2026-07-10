<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Jet on PiZZa

A PHONG-lit spinning Boing ball on Zephyr / BCM2710. Software 3D
rasteriser to a 912x492 RGB565 backbuffer, async DMA into the VideoCore
scanout, HVS scales to the monitor. ARM pinned to 1.0 GHz. Runs on the
Raspberry Pi Zero 2 W.

## Prerequisites

```sh
git clone https://github.com/CubeCoders/Jet ~/github/Jet
```

Override with `-DJET_SRC_DIR=<path>` if yours lives elsewhere.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-

cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/Jet -d build-jet \
  -- -DTOOLCHAIN_HAS_GLIBCXX=ON
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-jet/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

`-DTOOLCHAIN_HAS_GLIBCXX=ON` is required so libstdc++ linker wiring
kicks in; Jet uses `std::vector` internally.

## Running

HDMI shows the scene. The USB CDC ACM shell (`/dev/cu.usbmodem*`) is up
for inspection (`kernel uptime`, `sensor get vc-thermal`); boot logs are
on the mini-UART at 115200.

See [`NOTES.md`](NOTES.md) for architecture, per-phase timing, layout,
and licensing.
