<!-- SPDX-License-Identifier: Apache-2.0 -->
# Doom on PiZZa: notes

Companion to [`README.md`](README.md).

## How it works

Links the untouched
[AlexOberhofer/sdl2-doom](https://github.com/AlexOberhofer/sdl2-doom)
game sources (pure C, Chocolate Doom lineage) against the shared
[`apps/lib/sdl2shim`](../lib/sdl2shim) and paints native 320x200 into a
BCM2835 framebuffer; the VC4 HVS scales to the monitor.

The sdl2-doom checkout is not modified. One `sed` in the build tree
rewrites `i_video.c`'s baked 3x software upscale back to 320x200 so the
HVS does the scale in hardware; every other TU is the upstream file.

Audio and video share the one HDMI cable via the shim's VC4 MAI backend.
The `doom` shell injects keys over USB CDC ACM, or route a paired BT HID
keyboard through [`apps/lib/btinput`](../lib/btinput).

## Playing over serial

Connect to the USB CDC ACM port for the `uart:~$` prompt:

```
doom key enter         start / advance menu
doom down right        hold Right
doom up right          release Right
doom tap fire 200      hold Fire for 200 ms then release
doom keys              list all key names
```

Boot logs are on the mini-UART, GPIO 14/15 at 115200.

## Music (OPL)

`CONFIG_DOOM_MUSIC_OPL=y` compiles the Chocolate Doom OPL subsystem and
folds the synth into the mixer as a `MIX_CHANNEL_POST` effect. Instruments
come from the WAD's GENMIDI lump; no external data is needed.

## Sim tuning

`CONFIG_DOOM_SIM_TUNING=y` boots straight to E1M1 for the sim gates; TCG
is too slow to reach the title screen interactively.

## Layout

```
Doom/
├── README.md
├── NOTES.md
├── CMakeLists.txt              links sdl2-doom + shim; bakes the WAD asset pack
├── Kconfig                     present backend, OPL music, scripted input
├── prj.conf                    24 MiB arena, TLS on for picolibc errno
├── boards/
│   ├── rpi_zero_2w.{conf,overlay}  HDMI + CDC ACM + HDMI-MAI
│   └── qemu_cortex_a53.conf
└── src/
    ├── shim_main.c             Zephyr entry -> SDL_main
    ├── shim_video.c            SDL renderer/texture -> present seam
    ├── shim_mixer.c            SDL_mixer core (SFX)
    ├── opl_glue.c              OPL synth -> MIX_CHANNEL_POST hook
    ├── doom_assets.c           embedded WAD fd layer
    ├── doom_pack.S             .incbin of the WAD
    ├── doom_present_{display,semihost,none}.c
    ├── doom_shell.c            `doom` key-injection shell (USB CDC)
    └── doom_scripted.c         scripted input timeline (sim)
```

## Licensing

Binary is GPL-2.0 by linking sdl2-doom. PiZZa glue and the shim are
Apache-2.0.
