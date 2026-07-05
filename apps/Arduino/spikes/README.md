# Phase 6 USB-upload spikes (rpi_zero_2w)

Throwaway proofs for the Arduino-on-PiZZa upload path. All hardware-GREEN
2026-06-26. These are *reference* — the real work is folding `cdc-loader`'s
logic into the ArduinoCore-zephyr loader. Plan doc:
`~/github/SS/notes/pizza-arduino-phase6.md`.

Build env (all spikes):

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE="$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-"
export ZEPHYR_BASE="$HOME/zephyrproject/zephyr"
export EXTRA_ZEPHYR_MODULES="$HOME/zephyrproject/modules/hal/broadcom"
source "$HOME/.zephyr-venv/bin/activate"
west build -p always -b rpi_zero_2w/bcm2710 -d <builddir> <spikedir>
```

Flash: `install-to-sdcard.sh /Volumes/RECOVERY <builddir>/zephyr/zephyr.bin`.
Console/logs on the mini-UART (`/dev/cu.usbserial-*` @ 115200).

## cdc-loader/ — THE model (picotool-style, in-process reload)

The chosen architecture. Firmware owns the SD; host sends `sketch.llext` over
USB CDC; firmware writes it and `llext_load`s it. A forever-loop sketch runs in
its own thread while the loader watches the CDC for the IDE's **1200-bps touch**;
on the touch it aborts the sketch, receives the new one, and swaps it
**in-process** (no reboot — BCM2710 has no `sys_reboot`). POR-persistent (auto-
loads `/SD:/sketch.llext` at boot). No MSC, so the SD/Storage stays usable.

Test:
```sh
west build ... -d build/cdc spikes/cdc-loader
# flash, boot, then:
~/.zephyr-venv/bin/python spikes/cdc-loader/upload.py /dev/cu.usbmodemXXXX \
    <tick-sketch-build>/sketch.llext     # does the touch, then sends
```
Proven: v1 -> v2 swap mid-loop, `llext_unload` rc 0, no reboot.
Gotcha: needs `CONFIG_UART_LINE_CTRL=y` or the touch (baud read) is invisible.

## tick-sketch/ — forever-loop test sketch

`setup()`/`loop()`-style llext that prints `[sketch <ver>] tick N` once a
second. Build twice with `-DEXTRA_CFLAGS=-DTICK_VERSION=\"v2\"` to get a
distinguishable v1/v2 for the swap test. Output is `sketch.llext` via
`west build -d <dir> -t sketch`.

## msc/ — SUPERSEDED (kept as dwc2/MSC reference)

The original mass-storage approach (SD appears as a USB drive). Proved dwc2
device + MSC enumerate, but dropped: MSC forces a host-vs-firmware fight over
the SD and would sacrifice the Storage library. cdc-loader replaces it.

## Next (the real loader integration)

1. Port cdc-loader's thread + baud-monitor + swap into the ArduinoCore-zephyr
   loader.
2. CDC-sharing: in the real board the sketch's `Serial` *is* the CDC, so CDC RX
   is shared between `Serial.read` and the loader's upload receiver — route to
   the sketch normally, to the loader after the sketch is aborted.
3. `platform.txt`: replace the `sdfat` tool with a bundled CDC uploader;
   `use_1200bps_touch=true`.
4. Quiet logs: `USBD_CDC_ACM_LOG_LEVEL_WRN` + `USBD_LOG_LEVEL_WRN`.
