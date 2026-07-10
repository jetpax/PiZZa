<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mini vMac on PiZZa

A Motorola 68000 Macintosh Plus emulator on Zephyr / BCM2710. Boots
System 6.0.8 to an interactive Finder at 512x342 on the Raspberry Pi
Zero 2 W panel, driven by a paired Bluetooth keyboard and mouse (with a
USB CDC serial shell as fallback). Boot disk persists on the SD card.
Third consumer of the shared [`apps/lib/sdl2shim`](../lib/sdl2shim)
after Doom and SDLPoP.

## Prerequisites

Clone Mini vMac and generate the Mac Plus / SDL2 variation once:

```sh
git clone https://github.com/minivmac/minivmac ~/github/minivmac
cd ~/github/minivmac
gcc -o setup_t setup/tool.c
./setup_t -e bgc -t larm -cpu a64 -m Plus -api sd2 -sound 1 > setup.sh
bash setup.sh
```

Override the tree with `-DMINIVMAC_SRC_DIR=`, `-DMINIVMAC_ROM=`,
`-DMINIVMAC_DISK=` if yours live elsewhere. The build also needs the
Bluetooth patchram at `apps/lib/btinput/firmware/SYN43430A1.hcd`; the
blob is gitignored, see
[`apps/lib/btinput/firmware/PROVENANCE.md`](../lib/btinput/firmware/PROVENANCE.md)
for the fetch.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
```

Hardware (`rpi_zero_2w`, Claude builds, user flashes):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/minivmac -d build-minivmac-hw
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-minivmac-hw/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

Sim (`qemu_cortex_a53`, semihost PPM capture):

```sh
west build -p always -b qemu_cortex_a53 \
  -s ~/github/SS/PiZZa/apps/minivmac -d build-minivmac-qemu
west build -d build-minivmac-qemu -t run
```

## Running

HDMI shows the Finder. Put a Bluetooth keyboard or mouse into pairing
mode and power the Pi; the first boot pairs and stores the bond on the
SD, subsequent boots reconnect device-initiated. The `minivmac` CDC
shell on `/dev/cu.usbmodem*` is available as a serial fallback; boot
logs are on the mini-UART, GPIO 14/15 at 115200.

See [`NOTES.md`](NOTES.md) for architecture, the shell command
reference, phase status, and licensing.
