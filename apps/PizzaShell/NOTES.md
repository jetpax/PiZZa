<!-- SPDX-License-Identifier: Apache-2.0 -->
# PizzaShell: notes

Companion to [`README.md`](README.md).

## How it works

One Zephyr shell app for the whole Pi Zero family, compile-time gated so
a single source builds on both the Raspberry Pi Zero 2 W (BCM2710,
AArch64) and the original Pi Zero W (BCM2835, ARMv6). Boots into an
interactive shell over USB CDC ACM on the Zero 2 W, or the mini-UART on
the Zero W, and lights up the `pizza about` menu with whatever the board
supports (SoC, memory, storage, HDMI, Wi-Fi, VideoCore die temperature).

This is what a stock `pizza-shell-*.img.xz` release contains. The
[top-level README](../../README.md) covers the flash / first-boot walk-through.

## Common commands

```
uart:~$ pizza about
uart:~$ wifi scan
uart:~$ wifi connect -s <ssid> -p <password> -k 1
uart:~$ sensor get vc-thermal
uart:~$ hwinfo devid
uart:~$ device list
```

`pizza about` prints the same fields on either board and shows `--`
where the platform does not support them.

## Layout

```
PizzaShell/
├── README.md
├── NOTES.md
├── CMakeLists.txt         wires sample_usbd on boards with the USB device stack
├── Kconfig                pulls SAMPLE_USBD_* + Kconfig.zephyr
├── prj.conf               shell + line editing + logging defaults
├── boards/
│   ├── rpi_zero_2w.{conf,overlay}  full image: CDC ACM, HDMI, SD, Wi-Fi, sensors
│   └── rpi_zero_w.{conf,overlay}   console-only image: mini-UART + Wi-Fi
└── src/main.c             the `pizza about` menu, banner-on-connect, shell wiring
```
