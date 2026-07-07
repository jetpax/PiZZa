<!-- SPDX-License-Identifier: Apache-2.0 -->
# PiZZa · SDLPoP (Prince of Persia) via a minimal SDL2 shim

Port of [NagyD/SDLPoP](https://github.com/NagyD/SDLPoP) — the open-source
Prince of Persia remake, pure C — to PiZZa (Zephyr on the BCM2710 /
Cortex-A53), presenting through the proven Jet/wipeout framebuffer path.

The game tree is **never edited**. The device work is split between the
**shared SDL2 shim** ([`apps/lib/sdl2shim`](../lib/sdl2shim/README.md)) —
the game-agnostic half (init/event/timer/stub/posix, the audio backends,
USB bring-up, the SDL/dirent headers) — and the SDLPoP-specific glue kept
here, backed by the `bcm2835_fb` display driver with VideoCore HVS scaling.
Doom shares the same lib.

## Layout

```
apps/sdlpop/
├── CMakeLists.txt          links ~/github/SDLPoP/src (untouched) + the lib
├── Kconfig / prj.conf      present backend; audio via the lib's Kconfig
├── boards/
│   ├── rpi_zero_2w.{conf,overlay}   HDMI via vc_fb + HVS 320x200 scale
│   └── qemu_cortex_a53.conf         SIM target (semihost PPM capture)
├── include/SDL2/SDL_image.h + tools/pack_assets.py   (SDLPoP-specific)
└── src/                    SDLPoP-specific shim (diverges by frame/assets):
    ├── shim_main.c         Zephyr entry -> SDL_main
    ├── shim_video.c        renderer/texture -> present seam
    ├── shim_surface.c      surfaces, palette, blits (8/24/32 bpp)
    ├── shim_rw.c           SDL_RW* (mem-backed real; file via pack)
    ├── shim_image.c        IMG_Load_RW -> PIMG parse (no PNG decoder)
    ├── shim_shell.c        `pop` shell — key injection over USB CDC ACM
    ├── shim_scripted.c     synthetic input timeline (sim)
    ├── shim_selftest.c     color-bar video-path self-test
    ├── pop_assets.c        embedded asset pack fd layer (§4)
    ├── pop_pack.S          .incbin of the generated pack
    ├── pop_present_display.c   HW: bcm2835_fb + HVS
    ├── pop_present_semihost.c  SIM: PPM frames via semihosting
    └── pop_present_none.c      link/sim fallback
```

The game-agnostic TUs (`shim_core/event/timer/stub/posix`, the `audio_none`
+ HDMI-MAI backends and `audio/` core, `shim_usb`, and `SDL2/SDL.h` +
`dirent.h`) now come from the shared lib; SDLPoP calls its seams through the
`s2s_*` prefix and pulls the audio-backend choice from the lib's Kconfig.
The audio core's host tests: `tests/audio_host/` (`make run`, now compiled
from the lib's `audio/`); report: `RUNTIME_REPORT_AUDIO.md`.

## Build

Toolchain stanza is the invariant PiZZa one (see the wipeout recipe).

**Hardware (rpi_zero_2w) — Claude builds, user flashes:**
```sh
cd ~/zephyrproject && source ~/.zephyr-venv/bin/activate
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf- \
west build -p always -b rpi_zero_2w -s ~/github/SS/PiZZa/apps/sdlpop -d build-sdlpop
```

**Sim (qemu_cortex_a53) — the sim-verified gates:**
```sh
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf- \
west build -p always -b qemu_cortex_a53 -s ~/github/SS/PiZZa/apps/sdlpop -d build-sdlpop-qemu
west build -d build-sdlpop-qemu -t run     # writes sdlpop_frame_*.ppm
```

`SDLPoP` sources default to `~/github/SDLPoP/src` (override
`-DPOP_SRC_DIR=`); the `data/` tree next to it is packed automatically.

## Asset pipeline (§4 — preferred, executed)

`tools/pack_assets.py` walks `data/` (music/ excluded — this checkout
ships no oggs there anyway; OPL3 music uses the packed MIDISND DATs),
pre-converts every PNG to a raw **PIMG** blob (indexed pixels + RGBA
palette, `tRNS` preserved), and packs the tree into `sdlpop_assets.bin`,
linked into `.rodata` via `pop_pack.S`. `pop_assets.c` exposes it as a
read-only POSIX fd space, so the game's `fopen`/`fread` (via picolibc
tinystdio) and `SDL_RWFromFile` resolve with **no PNG decoder and no FS
driver on the critical path** — deterministic boot. SD+FAT remains the
documented fallback if asset iteration without reflashing is ever wanted.

## Backend seams (§5)

- **Audio (§5a): implemented** (lib backend). The lib's `shim_stub.c`
  audio entry points delegate to the `s2s_audio_*` table. On `rpi_zero_2w`,
  `CONFIG_SDL2SHIM_AUDIO_HDMI` selects the lib's HDMI-MAI backend: the
  game's 44.1 kHz callback mix is resampled to 48 kHz, packed into IEC958
  subframes and DMA-fed (DREQ 17) into the VC4 MAI FIFO on the same HDMI
  cable as video — no VCHIQ, no game change (the seam worked as designed).
  The `none` backend is the default elsewhere (qemu). Diagnostics:
  `pop audio`, `pop starve <n>`; M2 tone image via
  `CONFIG_SDL2SHIM_AUDIO_HDMI_TEST_TONE`. See `RUNTIME_REPORT_AUDIO.md`.
- **Input (§5b):** the lib's `shim_event.c` accepts events from any
  producer through the `s2s_event_submit` seam. `shim_scripted.c` is the
  sim producer (synthetic timeline); `shim_shell.c` is the **hardware**
  producer — a `pop` shell command on the USB CDC ACM line that injects
  keys, so you play over serial until a HOGP keyboard client attaches at
  the same seam.

## Playing over serial (hardware)

Connect to the USB CDC ACM port (`/dev/cu.usbmodem*`) for the `uart:~$`
prompt (boot logs are on the mini-UART, GPIO 14/15 @ 115200):

```
pop key enter        # dismiss "Press any key", menus, start
pop down right       # hold Right — the Prince runs right
pop up right         # release
pop tap right 400    # hold Right for 400 ms then release
pop key shift        # careful step / grab-ledge
pop key esc          # in-game menu
pop keys             # list all key names
```
Arrows move, Shift is careful/grab, Enter starts/continues, Esc opens
the menu — the stock Prince of Persia keyboard layout.

See `RUNTIME_REPORT.md` for sim-verified vs hardware-sign-off status.
