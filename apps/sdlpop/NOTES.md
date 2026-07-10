<!-- SPDX-License-Identifier: Apache-2.0 -->
# SDLPoP on PiZZa: notes

Companion to [`README.md`](README.md).

## How it works

Port of [NagyD/SDLPoP](https://github.com/NagyD/SDLPoP), the open-source
Prince of Persia remake (pure C), to Zephyr / BCM2710 / Cortex-A53. Video
goes through the shared [`apps/lib/sdl2shim`](../lib/sdl2shim) and the
BCM2835 framebuffer with VC4 HVS scaling; SFX + music come out of HDMI
via the shim's MAI backend on the same cable. Doom shares the same lib.

The SDLPoP tree is not edited. The game-agnostic half of the port lives
in [`apps/lib/sdl2shim`](../lib/sdl2shim); the SDLPoP-specific glue
(assets, present backend, shell) lives here.

## Assets

`tools/pack_assets.py` walks `data/`, pre-converts every PNG to a raw
**PIMG** blob (indexed pixels + RGBA palette, `tRNS` preserved), and packs
the tree into `sdlpop_assets.bin`. `pop_pack.S` links it into `.rodata`;
`pop_assets.c` exposes it as a read-only POSIX fd space so the game's
`fopen`/`fread` (via picolibc tinystdio) and `SDL_RWFromFile` resolve
with no PNG decoder and no FS driver on the critical path. SD+FAT
remains the documented fallback if you want to iterate assets without
reflashing.

## Playing over serial

Connect to the USB CDC ACM port (`/dev/cu.usbmodem*`) for the `uart:~$`
prompt; boot logs are on the mini-UART, GPIO 14/15 at 115200:

```
pop key enter        dismiss "Press any key", menus, start
pop down right       hold Right (the Prince runs right)
pop up right         release
pop tap right 400    hold Right for 400 ms then release
pop key shift        careful step / grab-ledge
pop key esc          in-game menu
pop keys             list all key names
```

Arrows move, Shift is careful/grab, Enter starts/continues, Esc opens the
menu; the stock Prince of Persia keyboard layout.

## Audio

The lib's `shim_stub.c` audio entry points delegate to the `s2s_audio_*`
table. `CONFIG_SDL2SHIM_AUDIO_HDMI` selects the HDMI-MAI backend: the
game's 44.1 kHz callback mix is resampled to 48 kHz, packed into IEC958
subframes and DMA-fed (DREQ 17) into the VC4 MAI FIFO on the same HDMI
cable as video, with no VCHIQ and no game change. Diagnostics: `pop
audio`, `pop starve <n>`. See `RUNTIME_REPORT_AUDIO.md`.

## Layout

```
SDLPoP/
├── README.md
├── NOTES.md
├── CMakeLists.txt          links ~/github/SDLPoP/src (untouched) + the lib
├── Kconfig / prj.conf      present backend; audio via the lib's Kconfig
├── boards/
│   ├── rpi_zero_2w.{conf,overlay}   HDMI via vc_fb + HVS 320x200 scale
│   └── qemu_cortex_a53.conf         SIM target (semihost PPM capture)
├── include/SDL2/SDL_image.h + tools/pack_assets.py   SDLPoP-specific
└── src/
    ├── shim_main.c                     Zephyr entry -> SDL_main
    ├── shim_video.c                    renderer/texture -> present seam
    ├── shim_surface.c                  surfaces, palette, blits (8/24/32 bpp)
    ├── shim_rw.c                       SDL_RW* (mem-backed real; file via pack)
    ├── shim_image.c                    IMG_Load_RW -> PIMG parse (no PNG decoder)
    ├── shim_shell.c                    `pop` shell (USB CDC key injection)
    ├── shim_scripted.c                 synthetic input timeline (sim)
    ├── shim_selftest.c                 color-bar video-path self-test
    ├── pop_assets.c                    embedded asset pack fd layer
    ├── pop_pack.S                      .incbin of the generated pack
    ├── pop_present_display.c           HW: bcm2835_fb + HVS
    ├── pop_present_semihost.c          SIM: PPM frames via semihosting
    └── pop_present_none.c              link/sim fallback
```

## Licensing

This binary is GPL-3.0 by linking SDLPoP; the PiZZa glue and the shim
are Apache-2.0.

See `RUNTIME_REPORT.md` for sim-verified vs hardware sign-off status.
