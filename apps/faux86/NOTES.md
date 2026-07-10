<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Faux86 on PiZZa: notes

Companion to [`README.md`](README.md).

## How it works

Links the [ArnoldUK/Faux86-remake](https://github.com/ArnoldUK/Faux86-remake)
8086 emulator against the Raspberry Pi Zero 2 W and paints the video
output to HDMI via the VC firmware framebuffer; the VC4 HVS scales to
the monitor. First-light on hardware boots to the `A:\>` MS-DOS prompt.

Faux86 has its own 5-method `HostSystemInterface` so it does not use the
SDL2 shim. `src/faux86_host.cpp` implements the interface against the
Zephyr display, timer and (upcoming) input APIs.

The Faux86 checkout stays pristine. `CMakeLists.txt` gen-copies the core
into the build tree and swaps one `typedef` in `Types.h` (`size_t`
clashes with picolibc); every other TU is the upstream file.

Faux86 is a pure interpreter; no dynrec / RWX region / large arena is
required (contrast with the DOSBox port).

## Assets

The build embeds `${FAUX86}/data/asciivgarom.dat` + `pcxtbios.bin` +
`videorom.bin` + `rombasic.bin` + a boot floppy into rodata via
`faux86_pack.S`.

## Input

Not wired yet (open workstream); today it boots and runs whatever the
packed floppy autoexec triggers.

## Layout

```
faux86/
├── README.md
├── NOTES.md
├── CMakeLists.txt          gen-copies the Faux86 core; embeds ROMs + floppy
├── Kconfig                 present backend + scripted input option
├── prj.conf                C++17 + libstdc++, RTTI + exceptions, 16 MiB arena
├── boards/
│   ├── rpi_zero_2w.{conf,overlay}   HDMI + async DMA + mini-UART logs
│   └── qemu_cortex_a53.conf
└── src/
    ├── faux86_main.cpp             Zephyr entry -> Faux86 VM run loop
    ├── faux86_host.{h,cpp}         HostSystemInterface impl (video/timer/input)
    ├── faux86_clock.c              PIT / timer glue
    ├── faux86_pack.S               .incbin ROMs + boot floppy into rodata
    └── faux86_present_{display,semihost,none}.c
```

## Licensing

Binary is GPL-2.0 by linking Faux86.
