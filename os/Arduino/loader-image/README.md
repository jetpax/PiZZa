# PiZZa loader SD image

Flashable SD-card images that boot the **PiZZa Arduino loader** on either a
**Raspberry Pi Zero 2 W** or the **original Raspberry Pi Zero W**. Flash once;
then upload sketches over USB-CDC from the Arduino IDE — no Raspberry Pi OS
install, no manual file copying.

This is the interim "easy on-ramp"; the longer-term plan is a Zephyr boot manager
(see `notes/boot_manager.html`).

## Build

Both boards share the same pipeline: `dist-build.sh` produces the loader
binaries and `.../loader-image/make-image*.sh` wraps each into an SD image.

```sh
# Zero 2 W
cd PiZZa/os/Arduino && ./dist-build.sh                   # -> firmwares/zephyr-rpi_zero_2w_bcm2710.bin
cd loader-image && ./make-image.sh                       # -> pizza-loader-rpi_zero_2w.img

# Original Zero W
cd PiZZa/os/Arduino && ./dist-build.sh                   # -> firmwares/zephyr-rpi_zero_w_bcm2835.bin
cd loader-image && ./make-image-rpi-zero-w.sh            # -> pizza-loader-rpi_zero_w.img
```

Both `make-image*.sh` need **Docker** (they build the FAT image loop-free with
`mtools`, so no root / privileged mode, same on macOS and Linux). Each:

1. fetches the Raspberry Pi boot blobs (`bootcode.bin`, `start.elf`, `fixup.dat`,
   and the matching DTB — `bcm2710-rpi-zero-2-w.dtb` for the 2 W,
   `bcm2708-rpi-zero-w.dtb` for the original) from `raspberrypi/firmware`,
   pinned to a fixed commit (cached in `blobs/`),
2. lays down a single FAT32 partition with those blobs + the matching
   `config.txt` + `zephyr.bin` (from `../../../../ArduinoCore-zephyr/firmwares/`).

Pass a different loader binary as the first argument if needed, e.g.
`./make-image-rpi-zero-w.sh /path/to/zephyr.bin`.

## Flash

Raspberry Pi Imager → **Use custom** → pick `pizza-loader-rpi_zero_2w.img` or
`pizza-loader-rpi_zero_w.img`, or use balenaEtcher. Then put the card in the Pi
and connect the **inner USB (data) port** to your computer.

## What's on the card

A single FAT32 partition (label `PIZZA`) — the boot blobs, `config.txt`, and
`zephyr.bin`. Config differs by board:

| Field | Zero 2 W | Zero W (original) |
|---|---|---|
| `arm_64bit` | `=1` | *(absent, 32-bit)* |
| `kernel_address` | `0x200000` | `0x8000` |
| `disable_commandline_tags` | *(absent)* | `=1` |
| DTB | `bcm2710-rpi-zero-2-w.dtb` | `bcm2708-rpi-zero-w.dtb` |
| `enable_uart=1`, `core_freq=250` | ✓ | ✓ |

The rest of the card is unallocated; sketches are streamed into the running
loader over USB-CDC, so nothing else is needed.

> The default **USB-CDC** upload doesn't care about the partition label. The
> optional **SD-card-reader** upload method writes `sketch.llext` to the boot
> partition — point its mount at this card (label `PIZZA`) under Tools → Upload
> method if you use that fallback.

## Status

✅ **Zero 2 W — hardware-confirmed 2026-06-29.** Flashed straight from Raspberry
Pi Imager (**Use custom**), boots the loader, and a WiFi sketch
(`WiFiWebClient`) uploads over USB-CDC and runs (Wi-Fi join + HTTP 200). One
flash, no Raspberry Pi OS step.

✅ **Zero W (original) — hardware-confirmed 2026-07-01.** Flashed via Imager,
boots the loader, USB-CDC enumerates, Blink uploads and runs (ACT LED on BCM
GPIO 47). WiFi support pending — see the main
[README](../README.md#what-works).
