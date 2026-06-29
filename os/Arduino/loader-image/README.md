# PiZZA loader SD image

A flashable SD-card image that boots the **PiZZA Arduino loader** on a Raspberry
Pi Zero 2 W. Flash it once; then upload sketches over USB-CDC from the Arduino
IDE — no Raspberry Pi OS install, no manual file copying.

This is the interim "easy on-ramp"; the longer-term plan is a Zephyr boot manager
(see `notes/boot_manager.html`).

## Build

```sh
cd PiZZa/os/Arduino && ./dist-build.sh        # produces the loader (zephyr.bin)
cd loader-image && ./make-image.sh            # produces pizza-loader-rpi_zero_2w.img
```

`make-image.sh` needs **Docker** (it builds the FAT image loop-free with `mtools`,
so no root / privileged mode, same on macOS and Linux). It:

1. fetches the Raspberry Pi boot blobs (`bootcode.bin`, `start.elf`, `fixup.dat`,
   `bcm2710-rpi-zero-2-w.dtb`) from `raspberrypi/firmware`, pinned to a fixed
   commit (cached in `blobs/`),
2. lays down a single FAT32 partition with those blobs + `config.txt` +
   `zephyr.bin` (the loader, from `../../../../ArduinoCore-zephyr/firmwares/`).

Pass a different loader binary as the first argument if needed:
`./make-image.sh /path/to/zephyr.bin`.

## Flash

Raspberry Pi Imager → **Use custom** → pick `pizza-loader-rpi_zero_2w.img`, or use
balenaEtcher. Then put the card in the Pi and connect the **inner USB (data)
port** to your computer.

## What's on the card

A single FAT32 partition (label `PIZZA`) — the boot blobs, `config.txt`
(`kernel=zephyr.bin`, `arm_64bit=1`, mini-UART @115200), and `zephyr.bin`. The
rest of the card is unallocated; sketches are streamed into the running loader
over USB-CDC, so nothing else is needed.

> The default **USB-CDC** upload doesn't care about the partition label. The
> optional **SD-card-reader** upload method writes `sketch.llext` to the boot
> partition — point its mount at this card (label `PIZZA`) under Tools → Upload
> method if you use that fallback.

## Status

✅ **Hardware-confirmed 2026-06-29.** Flashed straight from Raspberry Pi Imager
(**Use custom**), boots the loader, and a WiFi sketch (WiFiWebClient) uploads over
USB-CDC and runs (Wi-Fi join + HTTP 200). One flash, no Raspberry Pi OS step.
