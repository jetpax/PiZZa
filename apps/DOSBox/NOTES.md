<!-- SPDX-License-Identifier: Apache-2.0 -->
# DOSBox-X on PiZZa: notes

Companion to [`README.md`](README.md).

## How it works

Links the [DOSBox-X](https://github.com/joncampbell123/dosbox-x) machine
core against the shared [`apps/lib/sdl2shim`](../lib/sdl2shim) and runs
it on the Raspberry Pi Zero 2 W with HDMI video, HDMI audio via VC4 MAI,
keyboard over the `dosbox` shell (USB CDC ACM) or a paired BT HID device
via [`apps/lib/btinput`](../lib/btinput). DOS/4GW binaries work via the
dynrec dual-map recipe; DOOM runs at ~16 fps at 1 GHz on hardware.

The DOSBox-X checkout is untouched. `src/sdlmain_embedded.cpp` replaces
the desktop chrome wholesale; the CPU core, DOS kernel, VGA, PIT and PIC
are the upstream files, selected per `notes/P2_SCOPE.md`.

## Assets

`dosbox_pack.S` `.incbin`s the assets tree (COMMAND.COM, autoexec, any
bundled disk images) into rodata; `dosbox_assets.c` serves them as a
read-only fd layer. Point at a different tree with `-DDOSBOX_ASSETS=<dir>`
and rebuild.

## Layout

```
DOSBox/
├── README.md
├── NOTES.md
├── CMakeLists.txt           links the DOSBox-X TU selection + the shims
├── Kconfig                  present backend + scripted input option
├── prj.conf                 C++17 + libstdc++, 128 MiB arena, dynrec pages
├── boards/
│   ├── rpi_zero_2w.{conf,overlay}   HDMI + CDC ACM + HDMI-MAI + BT HID
│   └── qemu_cortex_a53.{conf,overlay}
├── patch_dyncache.py        dual-map RW/RX aliases for the dynrec cache
└── src/
    ├── sdlmain_embedded.cpp desktop chrome replacement (no window/menus)
    ├── dosbox_video.c       texture surface -> present seam
    ├── dosbox_present_{display,semihost,none}.c
    ├── dosbox_assets.c      embedded asset fd layer
    ├── dosbox_pack.S        .incbin of the asset tree
    ├── dosbox_clock.c       PIT / timer glue
    ├── dosbox_dyncache.c    dynrec code-cache arena (dual-map)
    ├── dosbox_btinput.c     paired BT HID keyboard -> DOS keys
    ├── dosbox_scripted.c    scripted keyboard timeline (sim)
    └── compat/  abi_thunks.cpp  lib_stubs.cpp  posix_stubs.cpp  sdl_compat.cpp
```

## Licensing

Binary is GPL-2.0 by linking DOSBox-X. PiZZa glue is Apache-2.0.
