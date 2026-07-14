<!-- SPDX-License-Identifier: Apache-2.0 -->
# RetroPiZZa

A libretro launcher for PiZZa (Zephyr / BCM2710). Boots to a core picker,
loads `.llext` "cores" from the SD card, browses content, and swaps cores
in-process — no reflash to change games. Cores are two kinds: wrapped
PiZZa shim apps (Doom, sokoban) via the `sdl2shim`-over-libretro glue, and
upstream libretro cores (2048, and the door M2 opened). Video is the
core's native frame scaled to the monitor by the VideoCore HVS (per-core
framebuffer sizing); audio (SFX + OPL FM music) goes out the HDMI via the
VC4 MAI, produced on a dedicated audio-first thread so it stays clean even
when a core saturates the CPU; input is a paired Bluetooth pad via
[`apps/lib/btinput`](../lib/btinput) or the `retro` USB CDC shell. Runs on
the Raspberry Pi Zero 2 W (audio needs `hdmi_drive=2` — already in the
shipped `config.txt` — and a display that accepts HDMI audio).

Master volume is global across cores: `CONFIG_RETRO_AUDIO_GAIN_PERCENT` at
build time, `retro vol <0-200>` live on the shell.

## Prerequisites

```sh
git clone https://github.com/libretro/libretro-2048 ~/github/libretro-2048
```

Cores are built separately from the launcher (each produces a `.llext`):
`core2048`, [`apps/Doom/core`](../Doom/core), `apps/sokoban/core`. Content
(WADs/ROMs) is not built in — it lives on the card.

## Build

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
export LIBRETRO_2048_SRC_DIR=$HOME/github/libretro-2048
```

Launcher, hardware (`rpi_zero_2w`, Claude builds, user flashes):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/retro -d build-retro-hw \
  -- -DEXTRA_DTC_OVERLAY_FILE=overlays/render640.overlay
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-retro-hw/zephyr/zephyr.bin
diskutil eject /Volumes/PIZZA
```

A core, e.g. Doom (produces `doom.llext`):

```sh
cd ~/zephyrproject
west build -p always -b rpi_zero_2w \
  -s ~/github/SS/PiZZa/apps/Doom/core -d build-doomcore-hw -t doom_core
```

## SD card layout

The launcher scans the FAT volume (`/SD:`) for cores and content:

```
/SD:/cores/*.llext     core files (copy the built doom.llext, 2048.llext, ...)
/SD:/roms/*            content (WADs/ROMs); a core that needs content browses here
```

Copy a core and its content onto the card, then boot:

```sh
cp ~/zephyrproject/build-doomcore-hw/doom.llext /Volumes/PIZZA/cores/
cp ~/github/doom1.wad                            /Volumes/PIZZA/roms/
```

## Sim (`qemu_cortex_a53`)

qemu has no SD, so a sim build seeds the cores/content into a ram-disk and
captures frames as semihosting PPMs. See `overlays/qemu_content.conf` (a
content core + a WAD) and `overlays/qemu_menu.conf` (two-core swap) for the
exact `west build` env and overlay lines.
