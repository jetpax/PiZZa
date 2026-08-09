# Raspberry PiZZA —  Pi Zero with Zephyr and Arduino

Write sketches in the **Arduino IDE** and run them on a **Raspberry Pi Zero 2 W**
or the **original Raspberry Pi Zero W**. A small firmware on the SD card (a
Zephyr "loader") loads your compiled sketch as a runtime module — so after a
one-time setup, **Upload is one button: no SD swap, no manual re-flash.**

> **Boards Manager URL** (changed at v0.6.0 — if you added the old
> `/dev/os/Arduino/...` URL before then, replace it with this one)
> ```
> https://raw.githubusercontent.com/jetpax/PiZZa/main/apps/Arduino/package_pizza_index.json
> ```

## Which Pi do you have?

| Board | Silicon | Bits | Board menu entry |
|---|---|---|---|
| **Pi Zero 2 W** (quad-core, 2021+) | BCM2710 / Cortex-A53 | 64-bit | **PiZZa (Raspberry Pi Zero 2 W)** |
| **Pi Zero W** (single-core, 2017) | BCM2835 / ARM1176 | 32-bit | **PiZZa (Raspberry Pi Zero W)** |



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

## Step 2 — Flash the PiZZa card (one time)

The Arduino loader ships as one entry on the standard PiZZa card, so
there is no separate Arduino image to install.

1. Download the boot-menu image for your board from
   [PiZZa Releases](https://github.com/jetpax/PiZZa/releases):
   - **Pi Zero 2 W:** `pizza-menu-rpi_zero_2w-*.img.xz`
   - **Pi Zero W (original):** `pizza-menu-rpi_zero_w-*.img.xz`
2. Flash with **Raspberry Pi Imager** (*Choose OS → Use custom* → pick the
   `.img.xz`) or balenaEtcher — both read `.img.xz` directly, no unzip
   needed. `./flash-sdcard.sh <image>` does the same from a terminal.
3. Put the card in the Pi and power up. The boot menu appears; choose
   **Arduino**.

Your choice is remembered, so from then on the Pi boots straight into the
loader. Hold a button between **GPIO 17 and GND** at power-on to get the
menu back and switch to something else.

That's the only time you touch the SD card from the host. To update just
the loader later, without re-imaging:

```sh
./install-to-sdcard.sh --slot Arduino <loader zephyr.bin>
```


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

## Pin map

`digitalWrite(n, …)` takes the **Arduino pin number** (the `Dn` column), not
the 40-pin header position and not the BCM GPIO number. All three differ, and
they collide misleadingly on 7: GPIO 4 sits on header pin 7, but writing `7`
in a sketch drives D7, which is GPIO 6 over on header pin 31.

| Arduino | BCM GPIO | Header pin | Also |
| --- | --- | --- | --- |
| D0 | GPIO 15 | 10 | UART1 RX (`Serial1`) |
| D1 | GPIO 14 | 8 | UART1 TX (`Serial1`) |
| D2 | GPIO 4 | 7 | |
| D3 | GPIO 17 | 11 | |
| D4 | GPIO 27 | 13 | |
| D5 | GPIO 22 | 15 | |
| D6 | GPIO 5 | 29 | |
| D7 | GPIO 6 | 31 | |
| D8 | GPIO 23 | 16 | |
| D9 | GPIO 24 | 18 | |
| D10 | GPIO 8 | 24 | SPI0 CE0 |
| D11 | GPIO 10 | 19 | SPI0 MOSI |
| D12 | GPIO 9 | 21 | SPI0 MISO |
| D13 | GPIO 11 | 23 | SPI0 SCLK |
| D14 | GPIO 29 (2 W) / GPIO 47 (Zero W) | none | on-board ACT LED, `LED_BUILTIN` |
| D15 | GPIO 12 | 32 | PWM0, `analogWrite()` |
| D16 | GPIO 13 | 33 | PWM1, `analogWrite()` |

Both boards use the same map so sketches port unchanged, except D14, which
follows each board's on-board LED.

## What works

**Pi Zero 2 W:** GPIO, SPI, Wire (I²C), Serial (USB-CDC; mini-UART as `Serial1`),
**WiFi** (the `WiFi` library / brcmfmac), and **PWM / `analogWrite()`** on
**D15 = GPIO 12 (header pin 32)** and **D16 = GPIO 13 (header pin 33)** — the
only two header pins the BCM283x PWM block reaches. `analogWrite()` on any
other pin falls back to plain digital HIGH/LOW at half scale, so the on-board
ACT LED (`LED_BUILTIN`) cannot fade. See the bundled `Fade` example.

**Pi Zero W (original):** GPIO, Serial (USB-CDC; mini-UART as `Serial1`), and
**WiFi** (new in 0.5.0 — same `WiFi` library / brcmfmac driver as the 2 W).
PWM is built into the Zero W loader too (same driver and D15/D16 map) but is
not hardware-verified there yet.
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
