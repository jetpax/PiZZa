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
| `os/Arduino/sketch/` | The Phase 3 reference sketch — a bare-C `int main()` that `printk`s a Hello string. Used as the smoke test for the SD-FAT load path |
| `os/Arduino/cxx-probe/` | Phase 1 standalone C++ recipe validator (global ctor / virtual dispatch / `__cxa_guard` / `std::array`). Kept as a regression baseline for the `CONFIG_GLIBCXX_LIBCPP + -DTOOLCHAIN_HAS_GLIBCXX=ON` recipe |
| `os/Arduino/examples/Blink/` | Standard Arduino Blink — toggles an LED on D13. Phase 5 reference sketch |
| `os/Arduino/examples/HelloSerial/` | Echo-on-Serial — prints once, echoes any byte received. Phase 5 reference sketch |
| `build.sh` | Orchestrates both builds (loader + Phase 3 sketch) against the local Zephyr workspace at `~/zephyrproject/`. Phase 4+ `.ino` sketches build via `arduino-cli` instead |

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

## Building a sketch (the IDE / CLI path — Phase 4+)

Once arduino-cli is set up with the symlinks in
[Local-dev wiring](#local-dev-wiring-phase-4), a stock Arduino sketch
builds via the usual one-liner:

```sh
arduino-cli compile -b arduino-git:zephyr:pizza --output-dir /tmp/out \
    os/Arduino/examples/Blink
```

`compile` produces `/tmp/out/Blink.ino.elf` — an AArch64 relocatable ELF
that IS the `.llext` (we override `pizza.upload.extension=elf` and skip
the upstream `zephyr-sketch-tool` header-wrap step). Deploy with:

```sh
arduino-cli upload --input-dir /tmp/out -b arduino-git:zephyr:pizza
```

which runs our `tools.sdfat` upload pattern — `cp ... && diskutil eject`
on macOS, `cp ... && sync && umount ...` on Linux. The SD mount point
defaults to `/Volumes/RECOVERY` (PINN), overridable per-OS in `boards.txt`.

## Local-dev wiring (Phase 4)

Until the platform ships through Arduino's package index, the dev
workflow needs three symlinks:

```sh
mkdir -p ~/Library/Arduino15/packages/arduino-git/hardware/zephyr
ln -sfn ~/github/SS/ArduinoCore-zephyr \
        ~/Library/Arduino15/packages/arduino-git/hardware/zephyr/9.9.9

mkdir -p ~/Library/Arduino15/packages/arduino-git/tools/aarch64-zephyr-elf
ln -sfn ~/zephyr-sdk/aarch64-zephyr-elf \
        ~/Library/Arduino15/packages/arduino-git/tools/aarch64-zephyr-elf/0.17.0

mkdir -p ~/github/SS/modules/lib
ln -sfn ~/github/SS/ArduinoCore-API \
        ~/github/SS/modules/lib/ArduinoCore-API
```

(The third one is so the in-tree
`cores/arduino/api -> ../../../modules/lib/ArduinoCore-API/api/`
symlink — which assumes a west workspace at `~/github/SS/` — resolves.)

After that, `arduino-cli board search pizza` will list
`arduino-git:zephyr:pizza`.

## Status

| Phase | What | Status |
|---|---|---|
| 0 | llext gating spike on aarch64 (`samples/subsys/llext/modules`) | DONE hw-GREEN |
| 1 | C++ runtime: `CONFIG_GLIBCXX_LIBCPP` + `-DTOOLCHAIN_HAS_GLIBCXX=ON` | DONE hw-GREEN |
| 2 | Variant `.overlay` + `.conf` + `boards.txt` FQBN entry | DONE files-only |
| 3 | Loader fork — SD-FAT path via `fs_loader`, `printk`-only sketch runs end-to-end | DONE hw-GREEN |
| 4 | `arduino-cli` build recipe + `tools.sdfat` upload tool | DONE — compile end-to-end via arduino-cli |
| 5 | Real Arduino sketches (Blink, HelloSerial) compile cleanly via arduino-cli | DONE compile-side; **runtime blocked by an SDHost driver bug, not by this port** |

### Phase 5 — the open driver issue (orthogonal)

Any sketch whose `fs_read` for section headers crosses the first 512-byte
SD block hangs the BCM2835 SDHost polled-PIO driver
(`drivers/sdhc/sdhc_bcm2835_sdhost.c` on the `pizero` branch of the
user's Zephyr workspace) with `CMD13: NEW_FLAG never cleared`. Phase 3's
1.5 KB sketch happened to fit in the first read; Phase 5's 6.9 KB Blink
and 14 KB HelloSerial both expose the bug. This is broader-port driver
work — flagged as a spinoff task; tracked in [`pizero` branch's HANDOVER.md].

Once that driver issue lands, Arduino sketches built via `arduino-cli`
should run unchanged.

Detail journal lives in `~/github/SS/notes/pizza-arduino-phase{0,1,2,3,4,5}.md`.
