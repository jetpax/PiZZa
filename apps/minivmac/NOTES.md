<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mini vMac on PiZZa: notes

Companion to [`README.md`](README.md).

## Status

| Phase | What | State |
| ----- | ---- | ----- |
| 0 | link: 68K core + SDL2 OSGLU + shim | done |
| 1 | boot ROM to the disk-request icon | done (sim) |
| 2 | 512x342 present on the panel (bcm2835_fb + HVS) | done (HW) |
| 3 | writable RAM-disk overlay, boot to Finder | done (sim) |
| 4/5 | keyboard + mouse via BT HID (Classic + BLE HOGP) + `minivmac` shell | done (HW) |
| 6 | persistent SD/FAT boot disk (`/SD:/minivmac.dsk`) | done (HW) |
| 7 | Mac audio via HDMI-MAI | deferred |

Per-stage reference screenshots are in [`notes/frames/`](notes/frames).

## How it works

Mini vMac's build system emits a single SDL2 "OS glue" file (`OSGLUSDL.c`)
plus the 68K core and device models. This port keeps that tree pristine
(pointed at by `MINIVMAC_SRC_DIR`) and links it against the shim, exactly
as the Doom and SDLPoP ports keep their game trees untouched. The only
PiZZa-specific code is the glue in `src/`.

- **Video.** The OSGLU drives a streaming SDL texture: it locks a 512x342
  ARGB8888 texture, writes the 1-bit Mac screen through its own CLUT,
  unlocks, then RenderCopy + RenderPresent. `minivmac_video.c` implements
  that texture / renderer surface app-local and hands the finished frame
  to the `s2s_present_*` backend. On hardware
  `minivmac_present_display.c` paints it into a 512x342 VideoCore
  framebuffer and the HVS scales to the monitor; on qemu
  `minivmac_present_semihost.c` writes PPM frames for the sim gates.
- **ROM + disk.** `minivmac_pack.S` `.incbin`s the Mac Plus ROM and a
  bootable System 6 floppy into rodata; `minivmac_assets.c` serves them
  through a POSIX fd layer (picolibc funnels `fopen`/`fread`/`fseek`
  through `open`/`read`/`lseek`). With `CONFIG_MINIVMAC_DISK_FS=y`
  (default on `rpi_zero_2w` and the qemu sim gate), `minivmac_storage.c`
  materialises the embedded disk into a real `minivmac.dsk` file on the
  SD FAT volume on first boot and services all subsequent reads/writes
  against it, so guest writes persist across reboots. Without the gate,
  the disk stays a RAM overlay (non-persistent).
- **Input.** Keyboard and mouse events reach the emulator through the
  shim's one `s2s_event_submit` producer seam. Three sources feed it:
  1. The [`apps/lib/btinput`](../lib/btinput) manager + SDL seam
     (`minivmac_btinput.c`): Classic BT HID (keyboard + mouse) plus BLE
     HOGP, connection policy and bond store on `/SD:/bt/settings`. No
     keymap remap: BT keyboard usages are SDL scancodes 1:1, so a paired
     8BitDo pad in K mode types letters into the Finder; a mouse drives
     the Mac cursor via the seam's absolute position, bounded to the
     512x342 Mac screen so it maps 1:1.
  2. The `minivmac` CDC shell (`minivmac_shell.c`), retained as a serial
     fallback / diagnostic.
  3. A scripted timeline (`minivmac_scripted.c`) for the qemu sim gate.

## Bluetooth keyboard + mouse

The `rpi_zero_2w` image ships with Bluetooth HID on by default: Classic
BT HID (keyboard + mouse) plus BLE HOGP, connection policy in the
[`apps/lib/btinput`](../lib/btinput) manager. Bonds persist on
`/SD:/bt/settings`.

Prerequisites:

- The Synaptics 43436S BT patchram at
  `apps/lib/btinput/firmware/SYN43430A1.hcd`. The blob is gitignored;
  see [`apps/lib/btinput/firmware/PROVENANCE.md`](../lib/btinput/firmware/PROVENANCE.md)
  for the fetch. The build fails at configure if it is missing.
- An SD card mounted at `/SD:` (the same card the boot disk lives on).

First pairing: put the device into pairing mode and power the Pi. On
boot, the manager's inquiry pages the first discoverable HID-class device
and completes SSP Just Works (or legacy pairing for older peripherals
like the 8BitDo Micro). The bond is written to `/SD:/bt/settings` and
device-initiated reconnects work on every subsequent boot with no
further action.

Move the mouse to move the Mac cursor; type on the keyboard to type into
the Finder.

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
├── NOTES.md
├── CMakeLists.txt           consumes sdl2shim.cmake; lists the Mini vMac TUs; bakes ROM + disk
├── Kconfig                  present-backend choice + scripted-input option
├── prj.conf                 common config (malloc arena, FPU, picolibc TLS)
├── boards/
│   ├── qemu_cortex_a53.conf sim: semihost PPM capture + scripted input
│   ├── rpi_zero_2w.conf     HW: display + BT HID + USB CDC shell (audio off)
│   └── rpi_zero_2w.overlay  vc_fb 512x342, CDC ACM, mini-UART console
├── notes/frames/            per-phase reference screenshots
└── src/
    ├── shim_main.c                     Zephyr entry; argv passes the boot disk
    ├── minivmac_video.c                SDL texture/renderer surface -> present seam
    ├── minivmac_present_{display,semihost,none}.c
    ├── minivmac_assets.c               ROM (read-only) + boot disk (RAM or SD/FAT) fd layer
    ├── minivmac_storage.c              persistent /SD:/minivmac.dsk backing (MINIVMAC_DISK_FS)
    ├── minivmac_pack.S                 .incbin ROM + floppy into rodata
    ├── minivmac_btinput.c              BT HID (keyboard + mouse) via apps/lib/btinput
    ├── minivmac_shell.c                `minivmac` CDC shell (fallback / diagnostic)
    └── minivmac_scripted.c             scripted input timeline (sim gate)
```

## Licensing

Mini vMac is GPL-2.0; the linked binary is GPL-2.0 by virtue of including
it. The PiZZa glue in `src/` and the shim are Apache-2.0, the same model
as the Doom and SDLPoP ports. The Macintosh Plus ROM and system disk are
Apple copyright and are not distributed in this repository; the build
embeds whatever the configured Mini vMac tree provides.
