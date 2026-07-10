<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# WipEout on PiZZa: notes

Companion to [`README.md`](README.md).

## How it works

Links the [phoboslab/wipeout-rewrite](https://github.com/phoboslab/wipeout-rewrite)
engine against the Raspberry Pi Zero 2 W and presents 320x240 frames via
the BCM2835 framebuffer; the VC4 HVS scales to the monitor. The
parallel-4 band renderer hits ~30 fps at 1 GHz on hardware.

The upstream tree is not edited:

- `src/render_software.c` is replaced with an SMP-patched copy
  (`render_software_smp.c`).
- The upstream `src/platform_*.c` files are replaced with
  `platform_zephyr.c`.
- `--wrap=file_exists / file_load / file_store` on the engine's utils.c
  routes SD-card I/O through Zephyr's `fs_*` on `/SD:` instead of libc
  `fopen()`.

FPU sharing is on (Cortex-A imply): the FPU-flush IPI rides mailbox-0
bit 1 next to the scheduler IPI, so V registers migrate safely between
cores. This is the gate for NEON in the rasteriser. A Q16 RGB565 PiZZa
renderer (`render_software_pizza.c`) is scaffolded but not yet
functional.

## Assets

Assets are the ~150 MB Sony PSX WipEout data (`wipeout-data-v01.zip`
from the upstream release). They live on the SD card at `/SD:/wipeout/`
on a FAT partition (mounted from partition 2 as `/DATA:`). Copy them
onto the card once; the game reads them at runtime, they are not baked
into the binary.

## Zephyr fork requirement

The `dev` branch of the jetpax Zephyr fork is required (the SMP wiring:
`SOC_PER_CORE_INIT_HOOK` gate plus the BCM2836 mailbox-0 IPI driver):

```sh
cd ~/zephyrproject/zephyr
git remote add jetpax https://github.com/jetpax/zephyr.git
git fetch jetpax
git checkout jetpax/dev
```

## Diagnostics

The `wipeout` shell on USB CDC ACM (`/dev/cu.usbmodem*`) carries the
diagnostics; boot logs are on the mini-UART at 115200:

```
wipeout fps                         current fps
wipeout flush parallel 4 64         bands 1..3 pinned on cpus 1..3
wipeout bench all                   scalar-vs-QPU perf table
```

The `WIPEOUT_USE_QPU_TRANSFORM` gate exists (offload
`vec3_transform_perspective` to the V3D QPUs) but is empirically slower
at per-triangle granularity than scalar; leave it off. See the Kconfig
help text for numbers.

## Layout

```
Wipeout/
├── README.md
├── NOTES.md
├── CMakeLists.txt         links wipeout-rewrite (untouched) + Zephyr platform layer
├── Kconfig                renderer choice (SMP / PiZZa) + QPU gate
├── prj.conf               C11, TLS on, malloc arena
├── boards/
│   └── rpi_zero_2w.{conf,overlay}   HDMI + async DMA + SD FAT + CDC ACM + SMP
└── src/
    ├── platform_zephyr.c        replaces upstream platform_*.c
    ├── render_software_smp.c    shipping SMP renderer (float ARGB_8888)
    ├── render_software_pizza.c  scaffolded Q16 RGB565 renderer (WIP)
    ├── gfx_parallel_zephyr.c    band worker threads, pinned per cpu
    ├── wipeout_glue.c           wipeout <-> Zephyr entry glue
    ├── wipeout_qpu.c            V3D QPU transform (empirically slower)
    └── vec3_xform.qasm          QPU shader source (assembled)
```

## Licensing

Binary is AGPL-3.0 by linking wipeout-rewrite. Kept out of PizzaShell so
the Apache-2.0 image stays clean.
