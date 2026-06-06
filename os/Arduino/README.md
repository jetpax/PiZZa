# PiZZA — Arduino on PiZZa (rpi_zero_2w)

The "A" in PiZZA is Arduino. This directory holds the PiZZa-specific
artifacts for running standard Arduino sketches on the Raspberry Pi
Zero 2 W via the official [ArduinoCore-zephyr][acz] project.

[acz]: https://github.com/arduino/ArduinoCore-zephyr

A sketch builds to a **freestanding ELF** (`.llext`) that the Zephyr
loader running on the Pi mounts the SD-FAT partition, opens the file
at `/SD:/sketch.llext`, links via Zephyr's `fs_loader`, and calls the
sketch's `main()` — **no kernel re-flash per sketch.** Edit, rebuild
the sketch, drop the new `.llext` on the SD card, reboot.

Source layout

| Path | What it is |
|---|---|
| `~/github/SS/ArduinoCore-zephyr/` (branch `pizza`) | Forked Arduino core; carries the loader + the `rpi_zero_2w_bcm2710` variant + the `pizza` `boards.txt` FQBN entry |
| `~/github/SS/ArduinoCore-API/` | Upstream Arduino API headers (Stream, Print, etc.) — sibling clone the cores include via `../../ArduinoCore-API/` |
| `os/Arduino/sketch/` | The Phase 3 reference sketch — a bare-C `int main()` that `printk`s a Hello string. The build target the toolchain produces |
| `os/Arduino/cxx-probe/` | Phase 1 standalone C++ recipe validator (global ctor / virtual dispatch / `__cxa_guard` / `std::array`). Kept as a regression baseline for the `CONFIG_GLIBCXX_LIBCPP + -DTOOLCHAIN_HAS_GLIBCXX=ON` recipe |
| `build.sh` | Orchestrates both builds (loader + sketch) against the local Zephyr workspace at `~/zephyrproject/` |

## Build + flash (current Phase 3 flow)

```sh
./build.sh
```

`build.sh` invokes `west build` twice — once for the loader (from
`ArduinoCore-zephyr/loader/`), once for the sketch (`os/Arduino/sketch/`).
Both go into `~/zephyrproject/build/pizza-arduino-{loader,sketch}/`.
The script ends by printing the one-line flash command:

```sh
~/zephyrproject/zephyr/boards/raspberrypi/rpi_zero_2w/support/install-to-sdcard.sh \
  /Volumes/RECOVERY \
  ~/zephyrproject/build/pizza-arduino-loader/zephyr/zephyr.bin \
  && cp ~/zephyrproject/build/pizza-arduino-sketch/sketch.llext \
        /Volumes/RECOVERY/sketch.llext \
  && diskutil eject /Volumes/RECOVERY
```

Boot output on the mini-UART (uart1, 115200) ends with:

```
*** Booting Zephyr OS build ... ***
Loading sketch from /SD:/sketch.llext
=== Hello from a PiZZA sketch! ===
(linked at boot from /SD:/sketch.llext via fs_loader)
```

## Why the loader lives in ArduinoCore-zephyr, not here

The Arduino build flow expects the variant overlay/conf to live under
`variants/<NORMALIZED_BOARD_TARGET>/` inside the core repo, and the
`boards.txt` FQBN entry to live in the same tree. Mirroring those into
PiZZa would either break the upstream build flow or require keeping
two copies in lock-step. The PiZZa-side artifacts are the *sketch*
sources; the *core* sources stay in the core repo.

## Deviations from stock ArduinoCore-zephyr (out-of-tree, by design)

Upstream Arduino's README explicitly does not accept PRs for new
targets, so PiZZA lives as a local variant only.

- **No internal flash, no `user_sketch` partition.** The Pi boots from
  the SD card; the loader (kernel image) and the sketch (`.llext` file)
  share the same FAT boot partition.
- **Sketch loader uses `fs_loader`** (`/SD:/sketch.llext`) instead of
  `flash_area_open(FIXED_PARTITION_ID(user_sketch))`. Switched at compile
  time by `CONFIG_ARDUINO_SKETCH_LOADER_FS=y` in the variant `.conf`.
- **aarch64, not Cortex-M.** The variant `.conf` carries the C++ recipe
  (`CONFIG_CPP / STD_CPP17 / GLIBCXX_LIBCPP` plus build-flag
  `-DTOOLCHAIN_HAS_GLIBCXX=ON`) and the MMU/llext settings
  (`LLEXT_HEAP_SIZE=512`, `LLEXT_RODATA_NO_RELOC=n`,
  `COMMON_LIBC_MALLOC_ARENA_SIZE=262144`). Toolchain is
  `aarch64-zephyr-elf-` from the Zephyr SDK.
- **No UF2 / 1200-bps-touch.** `pizza.upload.tool=none`; the deploy
  step is a file-copy to the FAT boot partition. Phase 4 wires this up
  as a real Arduino IDE programmer entry.
- **No ADC.** `analogRead` has no backing — documented; do not invent.
- **No PWM driver bound** in the in-tree DTSes — `analogWrite` will be
  unavailable until a BCM PWM driver is wired.
- **No on-board LED on the 40-pin header.** Blink needs an external
  LED on D13 (= GPIO 11 / SCK, header pin 23) per Arduino convention.

## Pin map (Arduino → BCM GPIO → header pin)

| Arduino | BCM | Header pin | Function |
|---|---|---|---|
| D0  | GPIO 15 | 10 | UART RX |
| D1  | GPIO 14 |  8 | UART TX |
| D2  | GPIO  4 |  7 | — |
| D3  | GPIO 17 | 11 | — |
| D4  | GPIO 27 | 13 | — |
| D5  | GPIO 22 | 15 | — |
| D6  | GPIO  5 | 29 | — |
| D7  | GPIO  6 | 31 | — |
| D8  | GPIO 23 | 16 | — |
| D9  | GPIO 24 | 18 | — |
| D10 | GPIO  8 | 24 | SPI CE0 |
| D11 | GPIO 10 | 19 | SPI MOSI |
| D12 | GPIO  9 | 21 | SPI MISO |
| D13 | GPIO 11 | 23 | SPI SCK |
| SDA | GPIO  2 |  3 | I2C1 SDA |
| SCL | GPIO  3 |  5 | I2C1 SCL |

## Build-time prerequisites

- `~/zephyrproject/` workspace, `pizero` branch (the PiZZa Zephyr port).
- `~/zephyr-sdk/aarch64-zephyr-elf/` Zephyr SDK with the aarch64
  toolchain installed.
- `~/.zephyr-venv/` with the Zephyr Python deps (`west`, `jsonschema`,
  …).
- `~/github/SS/ArduinoCore-zephyr/` on the `pizza` branch.
- `~/github/SS/ArduinoCore-API/` cloned as a sibling.
- `~/zephyrproject/modules/hal/broadcom/` (the upstream Zephyr Broadcom
  HAL is fetched via west update — not in the default allowlist; passed
  on the build line via `EXTRA_ZEPHYR_MODULES`).

## Status

Phase 0 (llext gating spike) — DONE hw-GREEN 2026-06-06.
Phase 1 (C++ runtime) — DONE hw-GREEN 2026-06-06.
Phase 2 (variant + board wiring) — DONE files-only 2026-06-06.
Phase 3 (loader fork for SD-FAT) — DONE hw-GREEN 2026-06-06.

Detail journal lives in `~/github/SS/notes/pizza-arduino-phase{0,1,2,3}.md`.
