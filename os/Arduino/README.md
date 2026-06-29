# PiZZA — Arduino on the Raspberry Pi Zero 2 W

Write sketches in the **Arduino IDE** and run them on a **Raspberry Pi Zero 2 W**.
A small firmware on the SD card (a Zephyr "loader") loads your compiled sketch as
a runtime module — so after a one-time setup, **Upload is one button: no SD swap,
no manual re-flash.**

> **Boards Manager URL**
> ```
> https://raw.githubusercontent.com/jetpax/PiZZa/dev/os/Arduino/package_pizza_index.json
> ```

## What you'll need

- A **Raspberry Pi Zero 2 W** — the quad-core *2 W*, not the original Zero / Zero W.
- A **microSD card** (≥ 2 GB) and a way to write it
  ([Raspberry Pi Imager](https://www.raspberrypi.com/software/)).
- A **micro-USB *data* cable** (not a charge-only cable).
- **Arduino IDE 2.x** on **Apple-Silicon macOS, Linux, or Windows**.
  > ⚠️ **Intel Macs are not supported** on 0.3.x — the upstream Zephyr SDK 1.0.1
  > has no Intel-Mac toolchain. Use an Apple-Silicon Mac / Linux / Windows, or
  > build on Linux and upload from the Intel Mac.

## Step 1 — Add the board to the Arduino IDE

1. **Settings / Preferences → Additional boards manager URLs** → add the URL above.
2. **Tools → Board → Boards Manager** → search **PiZZA** → **Install**. This
   downloads the core and the official **Zephyr SDK** AArch64 toolchain
   (~50 MB).
3. **Tools → Board → PiZZA (Raspberry Pi Zero 2 W)**.

## Step 2 — Flash the loader to the SD card (one time)

1. Download the ready-to-flash loader image —
   **[pizza-loader-rpi_zero_2w-v0.3.1.img.xz](https://github.com/jetpax/PiZZa/releases/download/arduino-loader-v0.3.1/pizza-loader-rpi_zero_2w-v0.3.1.img.xz)**
   (~2 MB; SHA-256 `a88834b9…`) from the
   [PiZZa loader-image release](https://github.com/jetpax/PiZZa/releases/tag/arduino-loader-v0.3.1).
2. Flash it with **Raspberry Pi Imager** (*Choose OS → Use custom* → pick the
   `.img.xz`) or balenaEtcher — both read `.img.xz` directly, no unzip needed.
3. (Recommended for 0.3.2) Replace `zephyr.bin` on the FAT32 boot partition
   (label `PIZZA`) with the newer loader binary at
   `<Arduino15>/packages/pizza/hardware/zephyr/0.3.2/firmwares/zephyr-rpi_zero_2w_bcm2710.bin`
   — gets you the reset-on-swap architecture (see *What's new in 0.3.2* below).
   The 0.3.1 image boots and runs fine without this step; the older loader just
   uses the older in-process swap path.
4. Put the card in the Pi.

That's the only time you touch the SD card from the host.

<details><summary>Advanced — build the image yourself / bring your own loader</summary>

The image is built reproducibly from the Raspberry Pi boot blobs + `config.txt` +
the loader (`zephyr.bin`) by `os/Arduino/loader-image/make-image.sh` in this repo.
Or start from a Raspberry Pi OS card and drop `zephyr.bin` (shipped in the
installed board package under
`<Arduino15>/packages/pizza/hardware/zephyr/<version>/firmwares/`) plus a
`config.txt` (`arm_64bit=1`, `kernel=zephyr.bin`, `enable_uart=1`,
`core_freq=250`, `kernel_address=0x200000`) on its boot partition.
</details>

## Step 3 — Your first sketch

1. Connect the Pi's **inner USB port** (labelled **USB**, the *data* one — not
   **PWR**) to your computer with the data cable. The board powers up and a serial
   port appears (`usbmodem…` macOS, `ttyACM…` Linux, `COM…` Windows).
2. **Tools → Port** → select it.
3. **File → Examples → PiZZA → Blink** → **Upload**. The on-board green **ACT
   LED** blinks. 🎉
4. Try **Examples → PiZZA → HelloSerial** and open the **Serial Monitor** — the
   sketch prints a banner and echoes any byte you send back.

From here, every **Upload** streams the sketch to the loader, which writes it
to the SD and triggers a cold reset of the Pi (~1 s) — when the loader comes
back up it auto-loads the new sketch. No card swap, no manual re-flash.

## Serial & upload

- **Serial = USB CDC.** Arduino `Serial` is the `usbmodem…` / `ttyACM…` port.
  `Serial1` is the mini-UART on **GPIO14 (TX) / GPIO15 (RX) @ 115200**, which is
  also the boot/log console — handy for watching the loader.
- **Upload (default) = USB CDC.** The Upload button streams the sketch over USB
  and resets the board to load it; just have the port selected. The uploader is
  a self-contained tool the Boards Manager fetched for your OS.
- **SD-card-reader upload** (*Tools → Upload method*) is the no-USB-host
  fallback: it copies `sketch.llext` to the SD's boot partition.
- **`LED_BUILTIN`** = the on-board green ACT LED (BCM GPIO 29).

## What works

**GPIO, SPI, Wire (I²C), Serial** (USB-CDC; mini-UART as `Serial1`), and **WiFi**
(the `WiFi` library / brcmfmac — `WiFiWebClient` connects and does HTTP).

Not available yet: **ADC / `analogRead`**, **PWM / `analogWrite`**. Libraries with
no driver wired in the variant overlay aren't shipped (Camera, Storage, SDRAM,
Ethernet, CAN, LED Matrix, RTC); they can be re-enabled as the drivers land.

## Under the hood

- **platform** `pizza:zephyr` — the PiZZA core (Arduino-on-Zephyr), source on
  the [`pizza` branch of jetpax/ArduinoCore-zephyr](https://github.com/jetpax/ArduinoCore-zephyr/tree/pizza),
  packaged as a release asset on [jetpax/PiZZa](https://github.com/jetpax/PiZZa/releases).
- **toolchain** `aarch64-zephyr-elf` **1.0.1** — the official
  [Zephyr SDK](https://github.com/zephyrproject-rtos/sdk-ng) AArch64
  cross-compiler, the *same* one the loader is built with, so your sketches
  are ABI-matched to the loader. Pulled straight from Zephyr SDK release
  tarballs.
- **uploader** `cdc-upload` — a small static tool that streams the sketch
  over USB CDC into the running loader.
- Your sketch compiles to a relocatable ELF and is loaded at boot as an
  `llext` module — the loader resets on each upload and the auto-load path
  picks up `/SD:/sketch.llext` on the next boot.

The manifest (`package_pizza_index.json`) is updated on each release with the
core URL, checksum, size, and the matching toolchain version.
