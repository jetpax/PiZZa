<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mini vMac on PiZZa

A Motorola 68000 **Macintosh Plus** emulator on PiZZa / Zephyr / BCM2710,
booting System 6 to an interactive Finder on the Raspberry Pi Zero 2 W panel.
The upstream Mini vMac emulator core runs unmodified against the shared
[`apps/lib/sdl2shim`](../lib/sdl2shim), the third consumer after Doom and
SDLPoP.

512x342, 1-bit, mouse + keyboard driven. Hardware-verified on rpi_zero_2w: the
Mac boots off an embedded System 6.0.8 floppy and is driven live over a USB CDC
serial shell.

## Status

| Phase | What | State |
| ----- | ---- | ----- |
| 0 | link: 68K core + SDL2 OSGLU + shim | done |
| 1 | boot ROM to the disk-request icon | done (sim) |
| 2 | 512x342 present on the panel (bcm2835_fb + HVS) | done (HW) |
| 3 | writable RAM-disk overlay, boot to Finder | done (sim) |
| 4/5 | keyboard + mouse via the `minivmac` shell | done (HW) |
| 6 | persistent SD/FAT disk | deferred |
| 7 | Mac audio via HDMI-MAI | deferred |

Per-stage reference screenshots are in [`notes/frames/`](notes/frames).

## How it works

Mini vMac's build system emits a single SDL2 "OS glue" file (`OSGLUSDL.c`) plus
the 68K core and device models. This port keeps that tree **pristine** (pointed
at by `MINIVMAC_SRC_DIR`) and links it against the shim, exactly as the Doom and
SDLPoP ports keep their game trees untouched. The only PiZZa-specific code is
the glue in `src/`.

- **Video.** The OSGLU drives a streaming SDL texture: it locks a 512x342
  ARGB8888 texture, writes the 1-bit Mac screen through its own CLUT, unlocks,
  then RenderCopy + RenderPresent. `minivmac_video.c` implements that texture /
  renderer surface app-local and hands the finished frame to the `s2s_present_*`
  backend. On hardware `minivmac_present_display.c` paints it into a 512x342
  VideoCore framebuffer and the HVS scales to the monitor; on qemu
  `minivmac_present_semihost.c` writes PPM frames for the sim gates.
- **ROM + disk.** There is no filesystem on the boot path. `minivmac_pack.S`
  `.incbin`s the Mac Plus ROM and a bootable System 6 floppy into rodata, and
  `minivmac_assets.c` serves them through a POSIX fd layer (picolibc funnels
  `fopen`/`fread`/`fseek` through `open`/`read`/`lseek`). The ROM is read-only;
  the disk is copied into a RAM overlay on first open so the guest OS can write
  to it (non-persistent, lost on reboot until Phase 6).
- **Input.** Keyboard and mouse events reach the emulator through the shim's one
  `s2s_event_submit` producer seam. Two sources feed it: the `minivmac` CDC
  shell (`minivmac_shell.c`) on hardware, and a scripted timeline
  (`minivmac_scripted.c`) for the qemu sim gate. The OSGLU uses absolute mouse
  coordinates, and the window is the native Mac screen, so they map 1:1.

## Prerequisites

The Mini vMac tree is not vendored. Clone it and generate the Mac Plus / SDL2
variation once:

```sh
git clone https://github.com/minivmac/minivmac ~/github/minivmac
cd ~/github/minivmac
gcc -o setup_t setup/tool.c
./setup_t -e bgc -t larm -cpu a64 -m Plus -api sd2 -sound 1 > setup.sh
bash setup.sh
```

That produces the gitignored `cfg/` variation headers (Mac Plus, 512x342,
1-bit); `src/` stays untouched. The tree also ships the Mac Plus ROM
(`extras/roms/vMac.ROM`) and a System 6.0.8 boot floppy (`extras/disks/608/`),
which the build embeds by default. Override with `-DMINIVMAC_SRC_DIR=`,
`-DMINIVMAC_ROM=`, `-DMINIVMAC_DISK=` if yours live elsewhere.

## Build and run

Toolchain env (invariant for these boards):

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
```

**qemu sim gate** (boots the Mac, runs the scripted input timeline, writes PPM
frames into the build dir):

```sh
cd ~/zephyrproject
west build -p always -b qemu_cortex_a53 -s ~/github/SS/PiZZa/apps/minivmac -d build-minivmac-qemu
west build -d build-minivmac-qemu -t run
```

**rpi_zero_2w hardware:**

```sh
west build -p always -b rpi_zero_2w -s ~/github/SS/PiZZa/apps/minivmac -d build-minivmac-hw
cd ~/github/SS/PiZZa
./install-to-sdcard.sh ~/zephyrproject/build-minivmac-hw/zephyr/zephyr.bin && diskutil eject /Volumes/RECOVERY
```

HDMI shows the Finder; the mini-UART (GPIO14, 115200) carries logs; the
`minivmac` shell is on the USB CDC ACM device.

## Driving it (the `minivmac` shell)

```
minivmac mouse <x> <y>      move the cursor (0..511 x 0..341)
minivmac click [x y]        click at coords or the current spot
minivmac moverel <dx> <dy>  move by a delta
minivmac key <name>         tap a key (down + up)
minivmac down|up <name>     hold / release (chords, e.g. cmd)
minivmac tap <name> [ms]    hold then release
minivmac keys               list key names
```

`<name>`: `left right up down return esc space tab backspace shift ctrl
alt/option cmd/gui a..z 0..9`. Example, open the System Folder:

```
minivmac mouse 62 98 ; minivmac click ; minivmac down cmd ; minivmac key o ; minivmac up cmd
```

## Layout

```
minivmac/
├── README.md
├── CMakeLists.txt           # consumes sdl2shim.cmake; lists the Mini vMac TUs; bakes ROM + disk
├── Kconfig                  # present-backend choice + scripted-input option
├── prj.conf                 # common config (malloc arena, FPU, picolibc TLS)
├── boards/
│   ├── qemu_cortex_a53.conf # sim: semihost PPM capture + scripted input
│   ├── rpi_zero_2w.conf     # HW: display + USB CDC shell (audio off)
│   └── rpi_zero_2w.overlay  # vc_fb 512x342, CDC ACM, mini-UART console
├── notes/frames/            # per-phase reference screenshots
└── src/
    ├── shim_main.c              # Zephyr entry; argv passes the boot disk
    ├── minivmac_video.c         # SDL texture/renderer surface -> present seam
    ├── minivmac_present_{display,semihost,none}.c
    ├── minivmac_assets.c        # ROM (read-only) + boot disk (RAM overlay) fd layer
    ├── minivmac_pack.S          # .incbin ROM + floppy into rodata
    ├── minivmac_shell.c         # `minivmac` keyboard + mouse shell (hardware)
    └── minivmac_scripted.c      # scripted input timeline (sim gate)
```

## Licensing

Mini vMac is GPL-2.0; the linked binary is GPL-2.0 by virtue of including it.
The PiZZa glue in `src/` and the shim are Apache-2.0, the same model as the Doom
and SDLPoP ports. The Macintosh Plus ROM and system disk are Apple copyright and
are not distributed in this repository; the build embeds whatever the configured
Mini vMac tree provides.
