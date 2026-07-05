<!-- SPDX-License-Identifier: Apache-2.0 -->
# PiZZa · SDLPoP (Prince of Persia) via a minimal SDL2 shim

Port of [NagyD/SDLPoP](https://github.com/NagyD/SDLPoP) — the open-source
Prince of Persia remake, pure C — to PiZZa (Zephyr on the BCM2710 /
Cortex-A53), presenting through the proven Jet/wipeout framebuffer path.

The game tree is **never edited**. All device work lives in a purpose-built
SDL2 shim that implements only the symbols SDLPoP actually references
(work-order §2 manifest, grep-verified), backed by the `bcm2835_fb` display
driver with VideoCore HVS hardware scaling.

## Layout

```
os/sdlpop/
├── CMakeLists.txt          links ~/github/SDLPoP/src (untouched) + shim
├── Kconfig / prj.conf      present + audio backend seams
├── boards/
│   ├── rpi_zero_2w.{conf,overlay}   HDMI via vc_fb + HVS 320x200 scale
│   └── qemu_cortex_a53.conf         SIM target (semihost PPM capture)
├── include/SDL2/SDL.h      the shim's SDL2 surface (only §2 symbols)
├── include/SDL2/SDL_image.h + include/dirent.h
├── tools/pack_assets.py    build-time PNG -> PIMG asset packer (§4)
└── src/
    ├── shim_core.c         init/error/version/window/misc
    ├── shim_video.c        renderer/texture -> present seam
    ├── shim_surface.c      surfaces, palette, blits (8/24/32 bpp)
    ├── shim_event.c        event queue + keyboard state (producer seam)
    ├── shim_timer.c        ticks/delay/perf on k_uptime/k_timer
    ├── shim_rw.c           SDL_RW* (mem-backed real; file via pack)
    ├── shim_image.c        IMG_Load_RW -> PIMG parse (no PNG decoder)
    ├── shim_stub.c         audio (C) delegation + pad/haptic (D) stubs
    ├── shim_posix.c        write/dir POSIX calls (read-only store)
    ├── pop_assets.c        embedded asset pack fd layer (§4)
    ├── pop_pack.S          .incbin of the generated pack
    ├── pop_present_display.c   HW: bcm2835_fb + HVS  (§ hardware)
    ├── pop_present_semihost.c  SIM: PPM frames via semihosting
    ├── pop_present_none.c      link/sim fallback
    ├── shim_audio_none.c    §5a seam — default where HDMI audio absent
    ├── shim_audio_hdmi.c    §5a REAL backend: HDMI audio via VC4 MAI
    ├── audio/               freestanding core (host-tested) + MAI init
    │   ├── iec958_pack.c    S16 -> IEC958 subframes (192-frame blocks)
    │   ├── resample_48k.c   147:160 polyphase (44.1k -> 48k), Q15
    │   ├── ring.c           cyclic DMA block-ring bookkeeping
    │   ├── audio_pump.c     source -> resample -> pack, one block/call
    │   └── hdmi_audio_init.c  MAI + audio infoframe + N/CTS (clean-room)
    ├── shim_scripted.c      §5b seam — synthetic input source
    ├── shim_selftest.c      color-bar video-path self-test
    └── shim_main.c          Zephyr entry -> SDL_main
```

Host tests for the audio core: `tests/audio_host/` (`make run`);
report: `RUNTIME_REPORT_AUDIO.md`.

## Build

Toolchain stanza is the invariant PiZZa one (see the wipeout recipe).

**Hardware (rpi_zero_2w) — Claude builds, user flashes:**
```sh
cd ~/zephyrproject && source ~/.zephyr-venv/bin/activate
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf- \
west build -p always -b rpi_zero_2w -s ~/github/SS/PiZZa/os/sdlpop -d build-sdlpop
```

**Sim (qemu_cortex_a53) — the sim-verified gates:**
```sh
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf- \
west build -p always -b qemu_cortex_a53 -s ~/github/SS/PiZZa/os/sdlpop -d build-sdlpop-qemu
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

- **Audio (§5a): implemented.** `shim_stub.c` audio entry points
  delegate to the `pop_audio_*` table. On `rpi_zero_2w`,
  `CONFIG_SDLPOP_AUDIO_HDMI` selects `shim_audio_hdmi.c`: the game's
  44.1 kHz callback mix is resampled to 48 kHz, packed into IEC958
  subframes and DMA-fed (DREQ 17) into the VC4 MAI FIFO on the same
  HDMI cable as video — no VCHIQ, no game or shim change (the seam
  worked as designed). `shim_audio_none.c` remains the default
  elsewhere (qemu). Diagnostics: `pop audio`, `pop starve <n>`;
  M2 tone image via `CONFIG_SDLPOP_AUDIO_HDMI_TEST_TONE`. See
  `RUNTIME_REPORT_AUDIO.md`.
- **Input (§5b):** `shim_event.c` accepts events from any producer.
  `shim_scripted.c` is the sim producer (synthetic timeline);
  `shim_shell.c` is the **hardware** producer — a `pop` shell command on
  the USB CDC ACM line that injects keys, so you play over serial until
  the shared HOGP keyboard client attaches at the same seam.

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
