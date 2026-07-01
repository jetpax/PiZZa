# PiZZa — Arduino on the Raspberry Pi Zero (2) W

Write sketches in the **Arduino IDE** and run them on a **Raspberry Pi Zero 2 W**
or the **original Raspberry Pi Zero W**. A small firmware on the SD card (a
Zephyr "loader") loads your compiled sketch as a runtime module — so after a
one-time setup, **Upload is one button: no SD swap, no manual re-flash.**

> **Boards Manager URL**
> ```
> https://raw.githubusercontent.com/jetpax/PiZZa/dev/os/Arduino/package_pizza_index.json
> ```

## Which Pi do you have?

| Board | Silicon | Bits | Board menu entry |
|---|---|---|---|---|
| **Pi Zero 2 W** (quad-core, 2021+) | BCM2710 / Cortex-A53 | 64-bit | **PiZZa (Raspberry Pi Zero 2 W)** |
| **Pi Zero W** (single-core, 2017) | BCM2835 / ARM1176 | 32-bit | **PiZZa (Raspberry Pi Zero W)** ⏳ WiFi is WIP|



## What you'll need

- A **Raspberry Pi Zero 2 W** *or* an **original Raspberry Pi Zero W**.
- A **microSD card** (≥ 2 GB) and a way to write it
  ([Raspberry Pi Imager](https://www.raspberrypi.com/software/)).
- A **micro-USB *data* cable** (not a charge-only cable).
- **Arduino IDE 2.x** on **Apple-Silicon macOS, Linux, or Windows**.
  > ⚠️ **Intel Macs are not supported** — the upstream Zephyr SDK 1.0.1 has no
  > Intel-Mac toolchain. Use an Apple-Silicon Mac / Linux / Windows, or build on
  > Linux and upload from the Intel Mac.

## Step 1 — Add the board to the Arduino IDE

1. **Settings / Preferences → Additional boards manager URLs** → add the URL above.
2. **Tools → Board → Boards Manager** → search **PiZZa** → **Install**. Boards
   Manager fetches the core and the matching **Zephyr SDK** toolchain(s) for the
   board(s) you use — AArch64 for the Zero 2 W (~50 MB), AArch32 for the
   original Zero W (~35 MB).
3. **Tools → Board → PiZZa (Raspberry Pi Zero 2 W)** *or* **PiZZa (Raspberry
   Pi Zero W)**, matching the board you're plugging in.

## Step 2 — Flash the loader to the SD card (one time)

1. Download the ready-to-flash loader image for your board:
   - **Pi Zero 2 W:** [pizza-loader-rpi_zero_2w-v0.4.0.img.xz](https://github.com/jetpax/PiZZa/releases/download/arduino-loader-v0.4.0/pizza-loader-rpi_zero_2w-v0.4.0.img.xz)
   - **Pi Zero W (original):** [pizza-loader-rpi_zero_w-v0.4.0.img.xz](https://github.com/jetpax/PiZZa/releases/download/arduino-loader-v0.4.0/pizza-loader-rpi_zero_w-v0.4.0.img.xz)

   Both are ~2 MB; SHAs on the [release page](https://github.com/jetpax/PiZZa/releases/tag/arduino-loader-v0.4.0).
2. Flash with **Raspberry Pi Imager** (*Choose OS → Use custom* → pick the
   `.img.xz`) or balenaEtcher — both read `.img.xz` directly, no unzip needed.
3. Put the card in the Pi.

That's the only time you touch the SD card from the host.


## Step 3 — Your first sketch

1. Connect the Pi's **inner USB port** (labelled **USB**, the *data* one — not
   **PWR**) to your computer with the data cable. The board powers up and a serial
   port appears (`usbmodem…` macOS, `ttyACM…` Linux, `COM…` Windows).
2. **Tools → Port** → select it.
3. **File → Examples → PiZZa → Blink** → **Upload**. The on-board green **ACT
   LED** blinks. 🎉
4. Try **Examples → PiZZa → HelloSerial** and open the **Serial Monitor** — the
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
- **`LED_BUILTIN`** = the on-board green ACT LED. **BCM GPIO 29** on the
  Zero 2 W; **BCM GPIO 47** on the original Zero W.

## What works

**Pi Zero 2 W:** GPIO, SPI, Wire (I²C), Serial (USB-CDC; mini-UART as `Serial1`),
and **WiFi** (the `WiFi` library / brcmfmac).

**Pi Zero W (original):** GPIO, Serial (USB-CDC; mini-UART as `Serial1`). SPI /
Wire / WiFi are pending — the BCM283x driver family is already ported for the
2 W and re-basing to BCM2835 is mostly mechanical, but each takes a bring-up
cycle.

- Not available on either yet: **PWM / `analogWrite`**.
- Not supported due to hardware limitation: **`analogRead()`** (no on-chip ADC,
but external SPI/I²C ADCs like ADS1115 or MCP3008 work).

## Troubleshooting

- **`arduino-cli upload` fails with "Serial port busy", or your terminal sees
  no output.** Quit the Arduino IDE (⌘Q on macOS). The IDE's Serial Monitor
  subprocess auto-grabs the port the moment it enumerates, and *respawns* if
  you kill it — only quitting the IDE releases the port for `arduino-cli`,
  `tio`, or `screen`.

## Under the hood

- **platform** `pizza:zephyr` — the PiZZa core (Arduino-on-Zephyr), source on
  the [`pizza` branch of jetpax/ArduinoCore-zephyr](https://github.com/jetpax/ArduinoCore-zephyr/tree/pizza),
  packaged as a release asset on [jetpax/PiZZa](https://github.com/jetpax/PiZZa/releases).
- **toolchain** `aarch64-zephyr-elf` **1.0.1** (Zero 2 W) *or*
  `arm-zephyr-eabi` **1.0.1** (original Zero W) — official
  [Zephyr SDK](https://github.com/zephyrproject-rtos/sdk-ng) cross-compilers,
  the *same* ones the matching loader is built with, so your sketches are
  ABI-matched to the loader. Pulled straight from Zephyr SDK release tarballs.
- **uploader** `cdc-upload` — a small static tool that streams the sketch
  over USB CDC into the running loader.
- Your sketch compiles to a relocatable ELF and is loaded at boot as an
  `llext` module — the loader resets on each upload and the auto-load path
  picks up `/SD:/sketch.llext` on the next boot.

The manifest (`package_pizza_index.json`) is updated on each release with the
core URL, checksum, size, and the matching toolchain version(s).
