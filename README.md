# 🍕 PiZZa  — Raspberry Pi Zero on Zephyr RTOS

Lightning boot time, no bloat and hard real time performance, all on a quad-core 1GHz microcontroller

Boots to a shell over a **USB-CDC ACM console** — plug a single
micro-USB cable from the Pi into your laptop and the Pi shows up as
a serial device. No external power supply, no USB-serial adapter, no
GPIO header soldering. BCM2710 peripherals, microSD, USB UDC, and
SDIO Wi-Fi (CYW43439) are all enabled. Runs on the **Raspberry Pi
Zero 2 W** and the original **Pi Zero W**.

The shipped card is a **boot-menu card**: PiZZaBoot picks at power-on
between RetroPiZZa, the Arduino sketch loader, MicroPython and the PiZZa
shell, and remembers the choice — see [Flash the SD card](#flash-the-sd-card).

PiZZa also has an  **Arduino board package**, so you can write sketches in the
Arduino IDE and Upload over the same single USB cable. The loader
receives the sketch, stores it on the SD card and runs it natively as
a Zephyr module (llext). No re-flash, no SD swap. Both boards are
supported. Full Arduino walk-through, upload methods and library status:
[apps/Arduino/README.md](apps/Arduino/README.md), as featured on
[hackster.io](https://www.hackster.io/news/pizza-turns-your-raspberry-pi-sbc-into-a-powerful-arduino-306bc23d06d9).

The [`apps/`](apps/) tree also carries the native ports that exercise the
platform: Doom, Prince of Persia (SDLPoP), MAc and Dos emulators, 4-core wipEout and the Jet 3d.
[RetroPiZZa](apps/retro/README.md) wraps them as a libretro launcher: pick
a core on the TV, load it off the SD card, play with a Bluetooth pad —
video scaled by the HVS, sound (SFX + FM music) out the HDMI.

This is the user-facing distribution side of the
**[jetpax/zephyr](https://github.com/jetpax/zephyr) fork**, which is
itself staging the upstream contribution under discussion in RFC
[zephyr#109880](https://github.com/zephyrproject-rtos/zephyr/issues/109880).
When the staged PRs merge into `zephyrproject-rtos/zephyr`, this repo
will simply point at upstream tags instead of the fork.

<img width="1590" height="1220" alt="image" src="https://github.com/user-attachments/assets/c639c635-40c3-427d-972d-97d92dab2b53" />


## Features

What's in the current PiZZa image, what's on the roadmap, and what's
out of scope. Legend: ✅ enabled · 🚧 planned · ❌ not planned · — N/A.

| Feature | Status | Notes |
| --- | :---: | --- |
| **Network** | | |
| Wi-Fi station (WPA2-PSK) | ✅ | CYW43439 SDIO via brcmfmac |
| Wi-Fi access point (softAP) | 🚧 | brcmfmac supports it; needs Zephyr wifi_mgmt glue |
| Bluetooth (CYW43439 BT side) | ✅ | Classic BR/EDR HID host + BLE HOGP via [`apps/lib/btinput`](apps/lib/btinput); used by [`apps/minivmac`](apps/minivmac) and [`apps/DOSBox`](apps/DOSBox). Not enabled in the default PizzaShell image. |
| Ethernet | — | no PHY on the Pi Zero 2 W |
| TCP/IP, DNS, DHCP client, HTTP server | ✅ | upstream Zephyr net stack |
| **Storage** | | |
| microSD card | ✅ | external slot via BCM283x SDHost |
| FAT / LittleFS mount | ✅ | upstream Zephyr FS stack |
| ext4 | ❌ | not in scope |
| **Buses & GPIO** | | |
| GPIO + interrupts | ✅ | BCM2711 family driver + bcm2835 pull control |
| SPI (SPI0) | ✅ | polled controller, loopback-tested |
| I²S / PCM | ✅ | DMA-driven; cyclic mode for streaming |
| I²C (BSC1) | ✅ | BCM2835 BSC driver, IRQ-driven; GPIO 2/3 ALT0, 100 kHz default. `i2c scan i2c@3f804000` from the shell. |
| PWM | ✅ | BCM283x PWM block, mark:space, both channels; GPIO 12/13 (header pins 32/33). |
| 1-Wire | ❌ | not planned |
| **Sensors / System** | | |
| Die-temperature sensor | ✅ | via VC firmware mailbox (`sensor get vc-thermal`) |
| Hardware RNG | ✅ | bcm2835-rng entropy driver |
| HWINFO (OTP board serial) | ✅ | via VC firmware (`hwinfo devid`) |
| **Console / Debug** | | |
| USB-CDC ACM over micro-USB | ✅ | primary; runs the Pi from host USB power |
| Mini-UART @ 115200 on GPIO 14/15 | ✅ | fallback, default config.txt |
| PL011 @ 1 Mbaud | ✅ | advanced; needs config.txt + DTS rebuild |
| **Display / Camera** | | |
| HDMI | ✅ | Framebuffer with EDID / mode auto-detect |
| MIPI DSI display | ❌ | not planned |
| MIPI CSI camera | ❌ | not planned |
| Composite video | ❌ | not planned |
| **CPU / Kernel** | | |
| AArch64 | ✅ | Cortex-A53; shipped shell images are single-core today |
| SMP (4 cores) | ✅ | spin-table boot + BCM2836 mailbox IPI + FPU sharing; kernel `smp` and `smp_stress` suites pass on hardware ([#1](https://github.com/jetpax/PiZZa/issues/1)). Opt-in per app: [`apps/Wipeout`](apps/Wipeout) renders across all four cores; the boot-menu entries are single-core |
| CPU frequency scaling | 🚧 | runs at idle clock (~600 MHz) from USB power |
| MMU + cache | ✅ | configured per the BCM2710 SoC tree |
| **USB** | | |
| Device mode (UDC, DWC2) | ✅ | drives the CDC ACM console |
| Host mode | ❌ | not in scope |

## Hardware compatibility

| Board | Supported? | Notes |
| --- | --- | --- |
| **Raspberry Pi Zero 2 W** | ✅ Yes — the target | Tested. BCM2710A1, Cortex-A53 quad, CYW43439 SDIO Wi-Fi. |
| **Original Raspberry Pi Zero W** | ✅ Yes | BCM2835, single-core ARM1176JZF-S (ARMv6, 32-bit, AArch32). USB-CDC console, SD storage, and (as of Arduino v0.5.0) on-module BCM43430A1 Wi-Fi all working — the loader downloads the CLM regulatory blob the trim-on-build firmware ships without. Sensor bring-up pending. The original Pi Zero (no Wi-Fi) is the same SoC and should work but is untested. |
| **Raspberry Pi 3 / 3B / 3B+** | ⚠️ Possibly — **untested** | Same Pi 3 / BCM27xx family (BCM2837), same Cortex-A53. Likely needs config tweaks for the different Wi-Fi part (BCM43438 vs CYW43439 → different firmware blob), Ethernet PHY, and HAT pin layout. Open an issue if you try it. |
| **Raspberry Pi 4 / 5** | ❌ No | BCM2711 / BCM2712, GIC-based, different MMIO base; uses the upstream [`rpi_4b`](https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_4b/doc/index.html) / `rpi_5` boards. |
| **Raspberry Pi Pico / Pico 2 (RP2040 / RP2350)** | ❌ No | Different SoC family entirely. |

## What you need

| Item | Notes |
| --- | --- |
| Raspberry Pi Zero 2 W or Zero W | Stock — no soldering, no additional hardware |
| microSD card, ≥ 4 GB | Flashed with a PiZZa image (see below) |
| Micro-USB cable | Connects the Pi's USB-OTG port to your laptop. Carries **both power and the console**. |
| Host computer | macOS, Linux, or Windows |

No separate 5 V supply is required: the Pi runs from the host's USB
port at the BCM2710's idle clock (~600 MHz). Total bench setup is a
laptop, a cable, and the Pi.

A USB-to-serial adapter (FTDI / CP210x / etc.) is **optional** — only
needed if you want to use the GPIO-header UART as a fallback console.
See [Console options](#console-options) at the bottom.

## Flash the SD card

Grab a `pizza-*.img.xz` from
[PiZZa Releases](https://github.com/jetpax/PiZZa/releases) and flash it
with [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
(**Use custom**) or balenaEtcher. That's the whole install: the image is
a single FAT32 boot partition carrying the pinned Raspberry Pi boot
blobs, `config.txt`, and the Zephyr apps.

`flash-sdcard.sh` does the same job from a terminal, taking either a
`.img` or a `.img.xz` and verifying the card by reading it back:

```sh
./flash-sdcard.sh pizza-menu-rpi_zero_2w-v0.7.0.img.xz
./flash-sdcard.sh pizza-menu-rpi_zero_2w-v0.7.0.img.xz /dev/disk6
```

With no device it lists the removable candidates and writes nothing. It
refuses partitions, internal disks, the disk backing `/`, and anything
over 128 GB — an external backup drive is otherwise indistinguishable
from a card — and it makes you retype the disk identifier before it
touches anything.

The stock image carries four apps and a boot menu:

| Entry | What it is |
| --- | --- |
| **RetroPiZZa** (default) | libretro launcher — cores and content off the SD card, HVS-scaled video and HDMI audio, Bluetooth pad ([`apps/retro`](apps/retro/README.md)) |
| **Arduino** | sketch loader for the Arduino IDE ([`apps/Arduino`](apps/Arduino/README.md)) |
| **MicroPython** | MicroPython REPL on the USB-CDC console |
| **PiZZa Shell** | the Zephyr shell image — peripherals, storage, Wi-Fi ([`apps/PizzaShell`](apps/PizzaShell/README.md)) |

The menu ([`apps/PiZZaBoot`](apps/PiZZaBoot)) draws on HDMI, the mini-UART
and the USB-CDC console at the same time. Pick with the arrow keys and
Enter, or with a button wired between **GPIO 17 and GND** — a short press
cycles, a long press boots. Your pick is written to `chosen.txt` and
becomes the default, so every later boot goes straight into that app with
no menu and no added boot time. Hold GPIO 17 low at power-on to force the
menu back. A fresh card ships no `chosen.txt`, so first boot always shows
the menu, with a five-second countdown to RetroPiZZa.

To image a card from a local build instead, use the builder at the repo
root (Docker, loop-free, works the same on macOS and Linux):

```sh
./make-sdcard.sh rpi_zero_2w path/to/zephyr.bin
./make-sdcard.sh rpi_zero_w  path/to/zephyr.bin
# extra payload files (game assets, disk images) are copied onto the FAT:
./make-sdcard.sh rpi_zero_2w build/doom/zephyr/zephyr.bin -o pizza-doom.img doom.img
```

`--menu` builds a boot-menu card instead: PiZZaBoot becomes the
`config.txt` kernel, each entry is staged as its own kernel, and the
first entry is the default.

```sh
./make-sdcard.sh rpi_zero_2w --menu build-bootmenu/zephyr/zephyr.bin \
  "RetroPiZZa=build-retro/zephyr/zephyr.bin" \
  "Arduino=build-arduino/zephyr/zephyr.bin" \
  "MicroPython=build-mpy/zephyr/zephyr.bin" \
  "PiZZa Shell=build-shell/zephyr/zephyr.bin" -o pizza-menu.img
```

## Update an existing card

To swap the Zephyr app on an already-flashed card:

1. **Download `zephyr.bin`** from the newest release, or build from
   source per the section below.
2. **Clone or download this repo:**

   ```sh
   git clone https://github.com/jetpax/PiZZa.git
   cd PiZZa
   ```

3. **Run the installer:**

   ```sh
   # macOS, card auto-mounted (PiZZa images mount as /Volumes/PIZZA)
   ./install-to-sdcard.sh ~/Downloads/zephyr.bin

   # original Pi Zero W (32-bit kernel, different load address)
   ./install-to-sdcard.sh rpi_zero_w ~/Downloads/zephyr.bin

   # Linux
   ./install-to-sdcard.sh /media/$USER/PIZZA ~/Downloads/zephyr.bin
   ```

   The script copies `zephyr.bin` into the boot partition and writes
   the board's `config.txt` (defaults to the Zero 2 W; pass
   `rpi_zero_w` for the original Zero W — the two are not
   interchangeable). Your previous `config.txt` is preserved as
   `config.txt.orig` on the first run.

   On a **boot-menu card** you must name the entry to replace with
   `--slot`. The script refuses without it and lists the available
   slots, and it never rewrites `config.txt` or `menu.txt` there — a
   plain install would otherwise overwrite the wrong kernel and take
   the menu's boot selector with it.

   ```sh
   ./install-to-sdcard.sh --slot RetroPiZZa ~/Downloads/retro.bin
   ```

4. **Eject** the card. The installer prints the right one-liner for your
   OS:

   ```sh
   # macOS
   diskutil eject /Volumes/PIZZA
   ```

5. **Insert** into the Pi and plug a micro-USB cable from the Pi's USB
   port to your laptop. The Pi will draw power and present a USB-CDC
   serial device in one go.

## First boot

A freshly flashed boot-menu card comes up in PiZZaBoot: the menu is on
HDMI and on both serial consoles, and it counts down five seconds to
RetroPiZZa. Pick **PiZZa Shell** to get the Zephyr shell described
below; that choice sticks until you change it from the menu (hold the
GPIO 17 button at power-on) or from the shell (`boot menu`, `boot
<name>`).

Once the Pi enumerates over USB it appears on the host as a serial
device. Open it with any terminal program — CDC ignores the baud
setting, but a sensible value keeps tools happy:

```sh
# macOS — the device usually shows up as /dev/tty.usbmodem*
ls /dev/tty.usbmodem*
tio /dev/tty.usbmodem1234 -b 115200

# Linux — usually /dev/ttyACM0
tio /dev/ttyACM0 -b 115200

# Windows — COMxx in Device Manager under "Ports (COM & LPT)"; PuTTY/Tera Term at 115200
```

You should see Zephyr boot and land at:

```
uart:~$
```

If no `usbmodem`/`ttyACM` device appears, fall back to the GPIO mini-UART
at **115200 baud, 8N1** — see [Console options](#console-options).

## Try things

```text
# Basics
uart:~$ kernel uptime
uart:~$ kernel version
uart:~$ device list

# Boot selection (boot-menu cards)
uart:~$ boot list                          # entries; * = the persisted choice
uart:~$ boot Arduino                       # persist and reboot into an entry
uart:~$ boot menu                          # drop the choice, reboot into the menu

# Hardware info
uart:~$ hwinfo devid                       # 64-bit OTP board serial
uart:~$ sensor get vc-thermal              # die temperature

# Storage (microSD)
uart:~$ device list                        # confirm SDHost is READY
uart:~$ mount /sd                          # if scripted; otherwise see below

# Wi-Fi (CYW43439 via brcmfmac).
# -k 1 = WPA2-PSK key-management (the common home-router setting);
# use -k 0 for an open network or -k 3 for WPA3-SAE.
uart:~$ wifi scan
uart:~$ wifi connect -s <ssid> -p <password> -k 1
uart:~$ wifi status
uart:~$ net iface
uart:~$ net dns google.com
```

## What's in the image

Release images are built from the
[`jetpax/zephyr`](https://github.com/jetpax/zephyr) `dev` branch at the
SHA listed on the corresponding GitHub Release. `dev` is the superset
that carries every `zp*` staging branch below plus the four-core SMP and
PWM work. It bundles:

| Subsystem | Driver | Source branch |
| --- | --- | --- |
| Board scaffold | rpi_zero_2w + bcm2710 SoC + BCM283x intc | [zp03](https://github.com/jetpax/zephyr/tree/zp03-rpi-zero-2w-board) |
| Primary console | USB-CDC ACM over micro-USB | [zp12](https://github.com/jetpax/zephyr/tree/zp12-usb-dwc2-bcm2710) |
| Fallback console | Mini-UART (uart1) on GPIO 14/15 @ 115200 | upstream |
| Timer | ARM architected timer | [zp01](https://github.com/zephyrproject-rtos/zephyr/pull/108775) (in review) |
| Mini-UART fixes | BCM2711 aux UART | [zp02](https://github.com/zephyrproject-rtos/zephyr/pull/108776) (in review) |
| GPIO | BCM2835 pull-control extension | [zp04](https://github.com/jetpax/zephyr/tree/zp04-gpio-legacy-pull) |
| Entropy | BCM2835 RNG | [zp06](https://github.com/jetpax/zephyr/tree/zp06-entropy-rng) |
| Thermal sensor | VideoCore die-temp via rpi_fw | [zp08](https://github.com/jetpax/zephyr/tree/zp08-vc-thermal) |
| HWINFO | Pi OTP board serial via rpi_fw | [zp07](https://github.com/jetpax/zephyr/tree/zp07-hwinfo) |
| SPI | BCM2835 SPI0 | [zp09](https://github.com/jetpax/zephyr/tree/zp09-spi-bcm2835) |
| DMA | BCM2835 DMA, single-block + cyclic | [zp10](https://github.com/jetpax/zephyr/tree/zp10-dma-bcm2835) |
| I²S | BCM2835 PCM / I²S | [zp11](https://github.com/jetpax/zephyr/tree/zp11-i2s-bcm2835) |
| USB UDC | DWC2 with BCM283x init fixes | [zp12](https://github.com/jetpax/zephyr/tree/zp12-usb-dwc2-bcm2710) |
| microSD (external slot) | BCM283x legacy SDHost | [zp13](https://github.com/jetpax/zephyr/tree/zp13-sdhc-bcm2835-sdhost) |
| SDIO (on-chip Wi-Fi bus) | Arasan SDHCI | [zp14](https://github.com/jetpax/zephyr/tree/zp14-sdhc-bcm2835-sdhci) |
| Wi-Fi | brcmfmac, native L2, WPA2-PSK | [zp16](https://github.com/jetpax/zephyr/tree/zp16-wifi-brcmfmac) |
| PWM | BCM283x PWM block (mark:space) + CM_PWM clock | [dev](https://github.com/jetpax/zephyr/tree/dev) (zp staging branch pending) |
| SMP | spin-table boot + BCM2836 mailbox IPI + FPU sharing | [dev](https://github.com/jetpax/zephyr/tree/dev) (zp staging branch pending) |
| Wi-Fi firmware blobs | **Bundled into `zephyr.bin`** at build time (via `hal_broadcom` + `west blobs fetch` from `rpi-distro/firmware-nonfree`). | [hal_broadcom](https://github.com/jetpax/hal_broadcom) |

## Version numbers

Two things here are versioned separately:

| Tag | Versions |
| --- | --- |
| `pizza-vX.Y.Z` | the distribution — the card image and everything on it |
| `arduino-core-vX.Y.Z`, `arduino-loader-vX.Y.Z` | the [Arduino board package](apps/Arduino/README.md) in Boards Manager, and its standalone loader image |

Boards Manager tracks the Arduino core on its own cadence, so the two
lines move independently. PiZZa is numbered above the core so the newest
number on the Releases page is always the distribution.

## Rebuilding from source

Release binaries live on
[Releases](https://github.com/jetpax/PiZZa/releases). To build your own:

```sh
# 1. Bring up a Zephyr workspace on the fork's dev branch
west init -m https://github.com/jetpax/zephyr --mr dev zephyrproject
cd zephyrproject
west update

# 2. Fetch the Wi-Fi firmware blobs (hal_broadcom is in the fork's west.yml)
west blobs fetch hal_broadcom

# 3. Build an app from this repo -- PizzaShell is the shell image
source ~/.zephyr-venv/bin/activate
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-
west build -p always -b rpi_zero_2w -s path/to/PiZZa/apps/PizzaShell
```

The boot-menu entries are `apps/PiZZaBoot` (the menu itself),
`apps/retro`, `apps/PizzaShell`, the
[ArduinoCore-zephyr](https://github.com/jetpax/ArduinoCore-zephyr) `pizza`
branch (built as `EXTRA_ZEPHYR_MODULES`, source dir `loader/`), and the
MicroPython Zephyr port. Feed the five `zephyr.bin` files to
`make-sdcard.sh --menu` as shown above.

When the upstream PRs land, this becomes a stock `west init` against
`zephyrproject-rtos/zephyr` and the fork drops out.

### Note on Wi-Fi firmware loading

Currently both the upstream-bound build and the PiZZa-bound build
compile the brcmfmac firmware into `zephyr.bin` via `hal_broadcom`
(the blobs step above). A future runtime FS-load path would let
`zephyr.bin` slim down by ~500 KB and read
`brcmfmac43436s-sdio.{bin,txt}` from the SD filesystem instead — but
that code is not yet in `zp16-wifi-brcmfmac`. Note: the firmware
files are not on the boot FAT of a stock Raspberry Pi OS card either;
they live on its ext4 root partition, which Zephyr cannot currently
read.

## Troubleshooting

**No USB-CDC device appears on the host.** Most often the Pi hasn't
fully booted yet — give it ~6 s after plug-in. If it still doesn't
appear, fall back to the GPIO mini-UART at 115200 (see
[Console options](#console-options)) to inspect the boot log directly.

**No output on the GPIO serial console either.** Check the baud is
**115200, 8N1**, and that you're wired to **GPIO 14 (TXD, Pi-side) /
GPIO 15 (RXD) / GND**, not the PL011/Bluetooth-shared pins
(`dtoverlay=disable-bt` is **not** set in the default config.txt).

**Installer says the card doesn't look like a Pi boot partition.**
The boot partition needs `bootcode.bin`, `start.elf`, and
`fixup.dat` already present — a PiZZa image has them. If the card was
imaged with plain Raspberry Pi OS instead, the mount point is
`/Volumes/bootfs` — just re-flash a PiZZa image.

**Wi-Fi connect fails.** Check `wifi scan` returns your SSID; check
`wifi status` for the actual disconnect reason. Open
[an issue](https://github.com/jetpax/PiZZa/issues) with the
console output.

**Boot hangs after `Starting kernel...`.** Most often the `kernel_address`
in `config.txt` doesn't match the image's link address. The bundled
config.txt has `kernel_address=0x200000` which is what the upstream
rpi_zero_2w board expects.

## Console options

The image ships with three console paths, in order of preference:

### 1. USB-CDC ACM (default — recommended)

Plug a micro-USB cable from the Pi's USB-OTG port to the host. The Pi
shows up as `/dev/tty.usbmodem*` (macOS), `/dev/ttyACM0` (Linux), or
a COM port (Windows). No external hardware, no separate power supply.
This is what the image is optimised for.

### 2. Mini-UART (uart1) on GPIO 14/15 @ 115200 — fallback

Wire a USB-to-serial adapter to GPIO 14 (Pi-TXD) / GPIO 15 (Pi-RXD) /
GND and open at **115200 baud, 8N1**. Every USB-serial adapter handles
this rate cleanly; the mini-UART's integer baud divisor is exact at
115200 with `enable_uart=1` locking the core clock to 250 MHz (which
the default `config.txt` does). This path is useful if the host can't
or won't enumerate the USB-CDC console, or if you want to use the
USB-OTG port for something else (a USB-host device, for instance).

### 3. PL011 (uart0) on GPIO 14/15 @ 1 Mbaud — advanced

The PL011 has a 16+6-bit fractional baud divider sourced from a 48 MHz
UART_CLK, where 1 000 000 lands on **exactly** IBRD=3, FBRD=0. Fast and
glitch-free, but requires (a) editing `config.txt` to add
`dtoverlay=disable-bt` (moves Bluetooth off PL011, freeing GPIO 14/15)
and `init_uart_baud=1000000`, (b) a rebuild with `zephyr,console = &uart0`
in the DTS, and (c) a USB-serial adapter that doesn't lie about its
clock at 1 Mbaud (some macOS adapters produce ~850 kbps when asked for
921 600 — logic-analyzer confirmed). Use this if you want a sub-millisecond
log channel for performance work; otherwise stick with USB-CDC.

## License

- Repo content: Apache-2.0 (see [LICENSE](LICENSE))
- Bundled `zephyr.bin` artifacts: Apache-2.0 (Zephyr OS) plus the
  brcmfmac firmware blobs under their own non-redistributable license,
  fetched at build time from `rpi-distro/firmware-nonfree`. The blob
  license text is in
  [`hal_broadcom/zephyr/blobs/license/LICENCE.broadcom_bcm43xx`](https://github.com/jetpax/hal_broadcom/blob/main/zephyr/blobs/license/LICENCE.broadcom_bcm43xx).

## Related

- [`jetpax/zephyr`](https://github.com/jetpax/zephyr) — the Zephyr fork staging the upstream contribution
- [`jetpax/hal_broadcom`](https://github.com/jetpax/hal_broadcom) — Zephyr module for the brcmfmac firmware blobs
- [`zephyrproject-rtos/zephyr`](https://github.com/zephyrproject-rtos/zephyr) — Zephyr upstream
