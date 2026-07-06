# G0a — First-light pristine boot

**Artifact:** `build/bootcode.bin` (102,064 bytes — pristine
librerpi/rpi-open-firmware, freshly built via the Docker toolchain).

This is the **cheapest possible test** — no suspend risk, no
integration risk. We are just proving that our toolchain + our SD
card + our silicon + our UART wiring can reach a VPU shell prompt.

## What you need

- Bench Pi Zero 2 W (dedicated — this replaces the boot chain
  completely)
- A dedicated microSD card (FAT32, small — this card CANNOT dual-boot;
  `bootcode.bin` is card-global)
- A 3.3 V USB-UART cable (CP2102 or FTDI — **must be 3.3 V, not 5 V**)
- USB power for the Pi

## SD-card contents

Format a small FAT32 card, then copy three files to its root:

```
bootcode.bin  <-  firmware/FreePiZZa/build/bootcode.bin
config.txt    <-  firmware/FreePiZZa/sd-card/config.txt
cmdline.txt   <-  firmware/FreePiZZa/sd-card/cmdline.txt   (optional at G0a)
```

That's it. No `start.elf`, no kernel — we own the VPU from mask ROM
on. `config.txt` is read by the mask ROM before our firmware runs;
it disables BT/Wi-Fi power drains, sets `gpu_mem=16`, and turns on
UART.

## UART wiring (mandatory — no CDC yet)

```
Pi Zero 2 W header             USB-UART cable
------------------             --------------
Pin 6  (GND)         -----     GND     (black)
Pin 8  (GPIO14 TXD)  -----     RX      (white/yellow)
Pin 10 (GPIO15 RXD)  -----     TX      (green)

DO NOT connect the 5 V/VCC wire. Power the Pi from its own USB port.
```

Serial: **115200 baud, 8N1, no flow control**.

## Boot and expected output

Open a terminal on the UART, insert the SD, apply USB power. You
should see, over the next second or two:

```
pre-pll hello
<... OTP dump ...>
CM_UARTCTL is 0x...
CM_UARTDIV is 0x...
PLLC.CORE0 <freq>
Booting Raspberry Pi....
Copyright 2016-2017 rpi-open-firmware authors
BUILDATE  : <date> <time>
SDRAM initialization completed successfully!
```

Then a VPU monitor prompt (`vpu#` or similar — it's whatever
`monitor_start()` prints).

## Success criteria (G0a)

- `SDRAM initialization completed successfully!` reaches the UART.
- Any prompt/response from the VPU monitor.

If that lands, our toolchain works, our SD workflow works, our
silicon is happy on open bootcode, and every downstream phase has a
foundation. If it *doesn't*, we investigate before touching suspend.

## Failure modes to distinguish

| Symptom | Likely cause |
| --- | --- |
| **Nothing at all on UART** | UART cable is 5 V not 3.3 V (fried?); TX/RX swapped; wrong baud; `enable_uart=1` missing |
| **`pre-pll hello` and nothing after** | PLLC bring-up failure (silicon variant?) or SDRAM init hang |
| **Gibberish** | Baud mismatch or UART clock wrong |
| **Garbled but recognisable** | PLL running at wrong frequency — investigate `xtal_freq` or PLLC config |
| **Boots but no monitor prompt** | Monitor's stdin path unwired — success anyway if `SDRAM init … completed` printed |

## When it works

Report the UART capture and we move to G0b: I apply the two
integration patches under `integration/`, rebuild in Docker (fast —
toolchain cached), and hand you the suspend-enabled `bootcode.bin`.
