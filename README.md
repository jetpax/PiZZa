# 🍕 PiZZa  — Raspberry Pi Zero with Zephyr

Boots to a shell over a **USB-CDC ACM console** — plug a single
micro-USB cable from the Pi into your laptop and the Pi shows up as
a serial device. No external power supply, no USB-serial adapter, no
GPIO header soldering. BCM2710 peripherals, microSD, USB UDC, and
SDIO Wi-Fi (CYW43439) are all enabled.

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
| Bluetooth (CYW43439 BT side) | 🚧 | shared silicon, separate HCI not yet exercised |
| Ethernet | — | no PHY on the Pi Zero 2 W |
| TCP/IP, DNS, DHCP client, HTTP server | ✅ | upstream Zephyr net stack |
| **Storage** | | |
| microSD card | ✅ | external slot via BCM283x SDHost |
| FAT / LittleFS mount | ✅ | upstream Zephyr FS stack |
| ext4 (for reading PINN-installed OS partitions) | ❌ | not in scope |
| **Buses & GPIO** | | |
| GPIO + interrupts | ✅ | BCM2711 family driver + bcm2835 pull control |
| SPI (SPI0) | ✅ | polled controller, loopback-tested |
| I²S / PCM | ✅ | DMA-driven; cyclic mode for streaming |
| I²C (BSC1) | ✅ | BCM2835 BSC driver, IRQ-driven; GPIO 2/3 ALT0, 100 kHz default. `i2c scan i2c@3f804000` from the shell. |
| PWM | 🚧 | planned |
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
| AArch64 single-core | ✅ | Cortex-A53, core 0 only |
| SMP (4 cores) | 🚧 | planned; upstream Zephyr BCM2710 limitation today |
| CPU frequency scaling | 🚧 | runs at idle clock (~600 MHz) from USB power |
| MMU + cache | ✅ | configured per the BCM2710 SoC tree |
| **USB** | | |
| Device mode (UDC, DWC2) | ✅ | drives the CDC ACM console |
| Host mode | ❌ | not in scope |

## Hardware compatibility

| Board | Supported? | Notes |
| --- | --- | --- |
| **Raspberry Pi Zero 2 W** | ✅ Yes — the target | Tested. BCM2710A1, Cortex-A53 quad, CYW43439 SDIO Wi-Fi. |
| **Original Raspberry Pi Zero / Pi Zero W** | 🚧 No | BCM2835, single-core ARM11 (ARMv6, 32-bit). Different architecture; this image is aarch64. |
| **Raspberry Pi 3 / 3B / 3B+** | ⚠️ Possibly — **untested** | Same Pi 3 / BCM27xx family (BCM2837), same Cortex-A53. Likely needs config tweaks for the different Wi-Fi part (BCM43438 vs CYW43439 → different firmware blob), Ethernet PHY, and HAT pin layout. Open an issue if you try it. |
| **Raspberry Pi 4 / 5** | ❌ No | BCM2711 / BCM2712, GIC-based, different MMIO base; uses the upstream [`rpi_4b`](https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_4b/doc/index.html) / `rpi_5` boards. |
| **Raspberry Pi Pico / Pico 2 (RP2040 / RP2350)** | ❌ No | Different SoC family entirely. |

## What you need

| Item | Notes |
| --- | --- |
| Raspberry Pi Zero 2 W | Stock — no soldering, no additional hardware |
| microSD card, ≥ 4 GB | Imaged with [PINN](https://github.com/procount/pinn) (see below) |
| Micro-USB cable | Connects the Pi's USB-OTG port to your laptop. Carries **both power and the console**. |
| Host computer | macOS, Linux, or Windows |

No separate 5 V supply is required: the Pi runs from the host's USB
port at the BCM2710's idle clock (~600 MHz). Total bench setup is a
laptop, a cable, and the Pi.

A USB-to-serial adapter (FTDI / CP210x / etc.) is **optional** — only
needed if you want to use the GPIO-header UART as a fallback console.
See [Console options](#console-options) at the bottom.

## Image the SD card with PINN

Use the official cross-platform
[Raspberry Pi Imager](https://www.raspberrypi.com/software/) (v2.0 or
later — available for macOS, Linux, and Windows). PINN is one of the
images it knows about:

1. Launch Pi Imager.
2. Choose your microSD card under **Storage**.
3. Under **OS** scroll to and click **Misc utility images**.

<img width="1584" height="1188" alt="image" src="https://github.com/user-attachments/assets/40a27b4f-a48c-42b5-9f1f-097fe0e21bcc" />


4. Pick **PINN — A multi-boot OS installer with OS admin features**.


<img width="1584" height="1188" alt="image" src="https://github.com/user-attachments/assets/7de96605-a242-423f-94b4-7825f6de18f3" />

5. Click **Next** → write. Eject when done.

PINN sets up the recovery partition (`bootcode.bin`, `start.elf`,
`fixup.dat`, `config.txt`) that Zephyr boots from. On macOS the
partition auto-mounts as `/Volumes/RECOVERY`; on Linux it's typically
`/media/$USER/RECOVERY`.

> **Why PINN?** Today the Zephyr image boots straight from the PINN
> recovery partition and doesn't strictly need a partition manager —
> a vanilla FAT SD card would work. PINN is in place so that future
> updates can land as a separate filesystem partition rather than a
> full kernel reflash. Starting on PINN means no re-imaging when that
> capability arrives.

## Install Zephyr

1. **Download `zephyr.bin`** from
   [PiZZa Releases](https://github.com/jetpax/PiZZa/releases/latest)
   (identical artifact mirrored on
   [`jetpax/zephyr` releases](https://github.com/jetpax/zephyr/releases/latest)).
   You can also build from source per the section below.
2. **Clone or download this repo:**

   ```sh
   git clone https://github.com/jetpax/PiZZa.git
   cd PiZZa
   ```

3. **Run the installer:**

   ```sh
   # macOS, PINN card auto-mounted at /Volumes/RECOVERY
   ./install-to-sdcard.sh ~/Downloads/zephyr.bin

   # Linux
   ./install-to-sdcard.sh /media/$USER/RECOVERY ~/Downloads/zephyr.bin
   ```

   The script copies `zephyr.bin` into the recovery partition and writes
   a new `config.txt` with safe defaults (64-bit mode, mini-UART fallback
   console at 115200, `kernel_address=0x200000`). Your previous
   `config.txt` is preserved as `config.txt.orig` on the first run.

4. **Eject** the card. The installer prints the right one-liner for your
   OS:

   ```sh
   # macOS
   diskutil eject /Volumes/RECOVERY
   ```

5. **Insert** into the Pi and plug a micro-USB cable from the Pi's USB
   port to your laptop. The Pi will draw power and present a USB-CDC
   serial device in one go.

## First boot

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

The image is built from
[`jetpax/zephyr`](https://github.com/jetpax/zephyr) at the SHA listed on
the corresponding GitHub Release. It bundles:

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
| Wi-Fi firmware blobs | **Bundled into `zephyr.bin`** at build time (via `hal_broadcom` + `west blobs fetch` from `rpi-distro/firmware-nonfree`). | [hal_broadcom](https://github.com/jetpax/hal_broadcom) |

## Rebuilding from source

Release binaries live on
[Releases](https://github.com/jetpax/PiZZa/releases). To build your own:

```sh
# 1. Bring up a Zephyr workspace per upstream docs
west init -m https://github.com/zephyrproject-rtos/zephyr zephyrproject
cd zephyrproject

# 2. Switch to the staged board branch on the fork
cd zephyr
git remote add jetpax https://github.com/jetpax/zephyr.git
git fetch jetpax
git checkout jetpax/zp16-wifi-brcmfmac    # has every dependency in

# 3. Register hal_broadcom locally (until the upstream west.yml entry lands)
cd ..
git clone https://github.com/jetpax/hal_broadcom.git modules/hal/broadcom

# 4. Fetch the firmware blobs
EXTRA_ZEPHYR_MODULES="$PWD/modules/hal/broadcom" \
  west blobs fetch hal_broadcom

# 5. Build
source ~/.zephyr-venv/bin/activate
EXTRA_ZEPHYR_MODULES="$PWD/modules/hal/broadcom" \
  west build -p always -b rpi_zero_2w -s zephyr/samples/subsys/shell/shell_module
```

When the upstream PRs land and `hal_broadcom` gets a `west.yml` entry,
the `EXTRA_ZEPHYR_MODULES` dance goes away.

### Note on Wi-Fi firmware loading

Currently both the upstream-bound build and the PiZZa-bound build
compile the brcmfmac firmware into `zephyr.bin` via `hal_broadcom`
(see step 3 above). A future runtime FS-load path would let
`zephyr.bin` slim down by ~500 KB and read
`brcmfmac43436s-sdio.{bin,txt}` from the SD filesystem instead — but
that code is not yet in `zp16-wifi-brcmfmac`. Note: the firmware
files are not on the PINN recovery partition itself; they ship
inside the per-OS images PINN can install (Raspberry Pi OS Lite,
etc.) on a separate ext4 root partition, which Zephyr cannot
currently read.

## Troubleshooting

**No USB-CDC device appears on the host.** Most often the Pi hasn't
fully booted yet — give it ~6 s after plug-in. If it still doesn't
appear, fall back to the GPIO mini-UART at 115200 (see
[Console options](#console-options)) to inspect the boot log directly.

**No output on the GPIO serial console either.** Check the baud is
**115200, 8N1**, and that you're wired to **GPIO 14 (TXD, Pi-side) /
GPIO 15 (RXD) / GND**, not the PL011/Bluetooth-shared pins
(`dtoverlay=disable-bt` is **not** set in the default config.txt).

**Installer says `RECOVERY` doesn't look like a Pi boot partition.**
The recovery partition needs `bootcode.bin`, `start.elf`, and
`fixup.dat` already present — those come from PINN. If the card was
imaged with plain Raspberry Pi OS instead, the mount point is
`/Volumes/bootfs`, not `/Volumes/RECOVERY` — use the upstream zephyr
port's `install-to-sdcard.sh` for that layout.

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
- [PINN](https://github.com/procount/pinn) — the bootloader/installer used on the SD card
