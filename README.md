# PiZZa — Zephyr on the Raspberry Pi Zero 2 W

> **Pi**·**Z**(ero) **2** (W) **a**(rch) — a pre-built [Zephyr
> RTOS](https://zephyrproject.org/) image for the Raspberry Pi Zero 2 W,
> ready to drop onto a [PINN](https://github.com/procount/pinn)-imaged
> SD card.

Boots to a shell over the PL011 UART with the BCM2710 peripherals,
microSD, USB UDC, and SDIO Wi-Fi (CYW43439) enabled.

This is the user-facing distribution side of the
**[jetpax/zephyr](https://github.com/jetpax/zephyr) fork**, which is
itself staging the upstream contribution under discussion in RFC
[#TBD](https://github.com/zephyrproject-rtos/zephyr) (link will be filled
in once the RFC is opened). When the staged PRs merge into
`zephyrproject-rtos/zephyr`, this repo will simply point at upstream tags
instead of the fork.

> **Status:** experimental. The board enablement is not yet merged
> upstream. Image bumps will land as new GitHub Releases here, not as
> commits to this repo.

## What you need

| Item | Notes |
| --- | --- |
| Raspberry Pi Zero 2 W | Stock — no soldering, no additional hardware |
| microSD card, ≥ 4 GB | Imaged with [PINN](https://github.com/procount/pinn) |
| USB-serial adapter capable of 1 Mbaud | The console is the **PL011** on GPIO 14/15, not the mini-UART. 1 Mbaud is the canonical rate. |
| Host computer | macOS, Linux, or Windows (with WSL or git-bash for the install script) |
| 5 V power supply | Standard micro-USB |

The PL011 baud-rate point is load-bearing: see the
[UART notes](#why-1-mbaud-on-pl011) at the bottom for context.

## Install (Mac / Linux)

1. **Image the SD card with PINN** if you haven't already. Follow the
   [PINN install instructions](https://github.com/procount/pinn#instructions);
   on macOS the recovery partition auto-mounts as `/Volumes/RECOVERY`.
2. **Download the latest `zephyr.bin`** from
   [Releases](https://github.com/jetpax/PiZZa/releases/latest)
   (or directly from
   [`jetpax/zephyr` releases](https://github.com/jetpax/zephyr/releases/latest)
   — the binary is the same).
3. **Clone or download this repo:**

   ```sh
   git clone https://github.com/jetpax/PiZZa.git
   cd PiZZa
   ```

4. **Run the installer:**

   ```sh
   # macOS, PINN card auto-mounted at /Volumes/RECOVERY
   ./install-to-sdcard.sh ~/Downloads/zephyr.bin

   # Linux
   ./install-to-sdcard.sh /media/$USER/RECOVERY ~/Downloads/zephyr.bin
   ```

   The script copies `zephyr.bin` into the recovery partition and writes
   a new `config.txt` with the right boot params (PL011 console at
   1 Mbaud, 64-bit mode, `kernel_address=0x200000`). Your previous
   `config.txt` is preserved as `config.txt.orig` on the first run.

5. **Eject** the card. The installer prints the right one-liner for your
   OS:

   ```sh
   # macOS
   diskutil eject /Volumes/RECOVERY
   ```

6. **Insert** into the Pi, connect the USB-serial adapter to GPIO
   14 (TXD, Pi-side) / 15 (RXD) and GND, and power up.

## First boot

Open the serial console at **1 Mbaud, 8N1**:

```sh
# macOS, adjust /dev/tty.usbserial-* to your adapter
tio /dev/tty.usbserial-10 -b 1000000

# Linux
tio /dev/ttyUSB0 -b 1000000

# Or screen / minicom / picocom, all at 1000000 baud
```

You should see Zephyr boot and land at:

```
uart:~$
```

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

# Wi-Fi (CYW43439 via brcmfmac)
uart:~$ wifi scan
uart:~$ wifi connect "<SSID>" "<PSK>"
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
| Console UART | PL011 (uart0) @ 1 Mbaud | upstream |
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
| Wi-Fi firmware | hal_broadcom module (blobs from `rpi-distro/firmware-nonfree`) | [hal_broadcom](https://github.com/jetpax/hal_broadcom) |

## Rebuilding from source

For the impatient there's a release binary on
[Releases](https://github.com/jetpax/PiZZa/releases). For
everyone else:

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

## Troubleshooting

**No output on the serial console.** First, double-check the baud rate
is **1000000**, not 115200. Second, you're on **GPIO 14/15 (PL011)**,
not the mini-UART pins — they're the same physical pins but a different
internal UART. If you flashed the image with the upstream `rpi_zero_2w`
helper instead of this one, that one writes a mini-UART config; re-run
this repo's `install-to-sdcard.sh`.

**`config.txt` says `RECOVERY` doesn't look like a Pi boot partition.**
The recovery partition needs to have `bootcode.bin`, `start.elf`, and
`fixup.dat` already present — those come from PINN's imager. If you
imaged the SD card with PINN they're there; if you used Raspberry Pi
Imager instead, see the upstream zephyr port's `install-to-sdcard.sh`
which targets `/Volumes/bootfs`.

**Wi-Fi connect fails.** Check `wifi scan` returns your SSID; check
`wifi status` for the actual disconnect reason. Open
[an issue](https://github.com/jetpax/PiZZa/issues) with the
console output.

**Boot hangs after `Starting kernel...`.** Most often the `kernel_address`
in `config.txt` doesn't match the image's link address. The bundled
config.txt has `kernel_address=0x200000` which is what the upstream
rpi_zero_2w board expects.

## Why 1 Mbaud on PL011?

The mini-UART (BCM AUX) has an integer-only baud divisor sourced from a
250 MHz clock; at high baud the nearest valid divisor produces > 2 %
error, marginal on adapters that don't auto-recover. The PL011 has a
16+6-bit fractional divider sourced from a 48 MHz UART_CLK, where a
baud rate of 1 000 000 lands on **exactly** IBRD=3, FBRD=0, no rounding
error.

921 600 was tried first but a common macOS USB-serial adapter (logic-
analyzer confirmed on the wire) produces ~850 kbps when asked for
921 600 — the device is clean, the host adapter is not. 1 Mbaud is
unambiguous on both sides.

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
