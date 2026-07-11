<!-- SPDX-License-Identifier: Apache-2.0 -->
# sdl2shim: notes

Companion to [`README.md`](README.md).

## What it provides

Game-agnostic translation units, grouped as the consuming CMake pulls them:

- **Core** (`SDL2SHIM_CORE_SOURCES`, always linked)
  - `shim_core.c`: init/quit, error string, version/hints, the single
    implicit "window", messagebox/iconv/name helpers.
  - `shim_event.c`: the SDL event queue + `SDL_GetKeyboardState` array,
    and the producer seam (`s2s_event_submit`) any input source feeds
    (scripted input, a shell command, a BT-keyboard thread). Derives
    `keysym.sym` from the scancode.
  - `shim_timer.c`: `SDL_GetTicks`/`Delay`, perf counters,
    `SDL_AddTimer` (via `k_timer`, callbacks in ISR context).
  - `shim_stub.c`: routes the SDL audio entry points to the backend
    seam, plus no-op joystick / game-controller / haptic accessors.
  - `shim_posix.c`: the POSIX odds-and-ends a hosted libc would supply.

- **Audio backend** (Kconfig `choice`, one of)
  - `SDL2SHIM_AUDIO_NONE` (`audio_none.c`): `s2s_audio_open` fails; the
    game runs silent. Used on qemu / boards with no audio.
  - `SDL2SHIM_AUDIO_HDMI` (`shim_audio_hdmi.c` + `src/audio/`): real
    sound out of HDMI via the VC4 MAI. The game's 44.1 kHz callback mix
    is resampled to 48 kHz, IEC958-packed, and DMA-fed (DREQ-paced)
    into the MAI FIFO. The freestanding, host-tested core is
    `src/audio/` (resample, IEC958 pack, cyclic ring, feeder pump,
    MAI/clock setup). `+HDMI_TEST_TONE` boots a 1 kHz tone for hardware
    sign-off.

- **Opt-in helpers**
  - `SDL2SHIM_USB` (`shim_usb.c`): USB CDC-ACM shell bring-up for a
    device console on the `rpi_zero_2w`.
  - `SDL2SHIM_SURFACE_SOURCES` (`shim_surface.c`): software surfaces,
    palettes, colorkey and the blit/convert family. Promoted from
    SDLPoP; required by the render / TTF / PIMG groups below.
  - `SDL2SHIM_RW_SOURCES` (`shim_rw.c`): memory-backed `SDL_RWops`
    (promoted from SDLPoP; `SDL_RWFromFile` fails clean -- files
    resolve through each app's pack fd layer).
  - `SDL2SHIM_RENDER_SOURCES` (`shim_render.c`): a software
    `SDL_Renderer`/`SDL_Texture` engine for games written against the
    renderer API proper (sokoban; SNES9x-class ports later). ARGB8888
    backing the size of the single window, malloc'd ARGB textures,
    nearest-scaled `RenderCopy` with NONE/BLEND, lines/rects,
    render-to-texture targets; `RenderPresent` pushes the backing
    through the same `s2s_present_*` seam the app already implements.
    Not implemented until a consumer needs them: `RenderCopyEx`,
    MOD/ADD blends, non-nearest filters, logical-size scaling.
  - `SDL2SHIM_TTF_ATLAS_SOURCES` (`shim_ttf_atlas.c` +
    `include/sdl2shim_atlas.h`): `SDL2_ttf` subset over baked "S2SA"
    glyph atlases -- no freetype on the device. The app's packer bakes
    one atlas per (font, size) under the pack key `<path>@<size>`;
    `TTF_RenderText_Blended` rasterises from A8 coverage.
  - `SDL2SHIM_IMAGE_PIMG_SOURCES` (`shim_image_pimg.c`): `SDL2_image`
    subset over build-time pre-converted PIMG blobs (promoted from
    SDLPoP -- its packer stays the format reference).

  The dual-target TUs (surface / rw / render / ttf-atlas / image-pimg)
  also build on the host via `src/s2s_compat.h`; `tests/render_host/`
  is a plain-`make` exact-value pixel suite over them.

  Scripted synthetic input is not a lib piece: the
  `s2s_scripted_input_start` seam is declared here, but each game
  supplies its own timeline app-local (Doom's `doom_scripted.c`,
  SDLPoP's `shim_scripted.c`).

- **Headers** (`include/`)
  - `SDL2/SDL.h`: the SDL2 API subset: init/error, the single window +
    drawable size, event queue + keyboard, timers + perf counters, the
    audio device seam, RWops, threading (`SDL_mutex`/`SDL_cond`),
    `SDL_MixAudioFormat`, and stubbed joystick/controller/haptic types.
  - `SDL2/SDL_mixer.h`: the SDL_mixer subset: the SFX `Mix_*` API,
    stubbed music entry points, and the
    `Mix_RegisterEffect`/`MIX_CHANNEL_POST` hook OPL music plugs into.
  - `SDL2/SDL_endian.h`: `SDL_SwapLE16/32` + endian constants (Doom's
    `i_swap.h` needs them).
  - `SDL2/SDL_image.h`: the SDL2_image subset over PIMG blobs.
  - `SDL2/SDL_ttf.h`: the SDL2_ttf subset over baked glyph atlases.
  - `sdl2shim_atlas.h`: the S2SA atlas format contract the per-app
    font bakers must mirror.
  - `dirent.h`: picolibc has none; a minimal directory-enumeration stub.

## What it does NOT do (by design)

- It is not real SDL. Only the calls the current consumers make exist.
  New symbols get added as new games need them.
- No frame present. Composition can now be app-side (SDLPoP blits 8-bit
  surfaces; Doom pushes a native 320x200 XRGB buffer; Mini vMac pushes
  a 512x342 1-bit screen through an ARGB8888 CLUT) or the opt-in
  `SDL_Renderer` engine (sokoban) -- but the present backend TU stays
  in the consuming app either way, behind `s2s_present_*`.
- The SDL_mixer `Mix_*` implementation is the app's (`shim_mixer.c`),
  not the lib's. Mixing, sample rate, and effect use diverge per game.
  The lib owns the header and the audio backend the mixer feeds, not
  the mixer core.
- No real filesystem. A consumer supplies a thin read-only fd layer
  over its embedded asset pack (Doom's WAD, SDLPoP's data, Mini vMac's
  ROM + floppy); disk writes fail EROFS. `dirent` only enumerates that.
- `main()` belongs to the app. The game's `main` is renamed to
  `SDL_main`; the app's `shim_main.c` is the Zephyr entry that calls it.
- No image decode or font rasterisation on the device: SDL_image and
  SDL_ttf exist only over build-time-baked assets (PIMG blobs, S2SA
  atlases). No net/thread-pool loaders.

## The lib / app split

The neutral, reusable half is here; the half that diverges per game
stays in the consuming app:

| Concern                                   | Where     |
| ----------------------------------------- | --------- |
| init, error, events, timers, POSIX        | **lib** (core) |
| audio backend (none / HDMI-MAI)           | **lib**   |
| surfaces / RWops / SDL_Renderer engine    | **lib** (opt-in) |
| SDL_image (PIMG) / SDL_ttf (S2SA atlas)   | **lib** (opt-in) |
| SDL_mixer `Mix_*` implementation          | app       |
| frame present backend (`s2s_present_*`)   | app       |
| asset store (fd layer over the pack)      | app       |
| asset/font/map bakers (`tools/`)          | app       |
| `main()` / Zephyr entry                   | app       |

## Layout

```
sdl2shim/
├── README.md
├── NOTES.md
├── Kconfig               audio-backend choice + log level
├── sdl2shim.cmake        source groups + include dirs for consumers
├── sdl2shim.h            INTERNAL seams (s2s_*), not seen by the game
├── include/
│   ├── SDL2/SDL.h        SDL2 subset
│   ├── SDL2/SDL_mixer.h  SDL_mixer subset
│   ├── SDL2/SDL_image.h  SDL2_image subset (PIMG)
│   ├── SDL2/SDL_ttf.h    SDL2_ttf subset (S2SA atlas)
│   ├── SDL2/SDL_endian.h
│   ├── sdl2shim_atlas.h  S2SA glyph-atlas format
│   └── dirent.h
├── src/
│   ├── shim_core.c  shim_event.c  shim_timer.c  shim_stub.c  shim_posix.c
│   ├── shim_usb.c
│   ├── s2s_compat.h      Zephyr/host compat for the dual-target TUs
│   ├── shim_surface.c  shim_rw.c  shim_render.c
│   ├── shim_ttf_atlas.c  shim_image_pimg.c
│   ├── audio_none.c      shim_audio_hdmi.c
│   └── audio/            freestanding HDMI-MAI core (host-tested):
│       ├── resample_48k.*  iec958_pack.*  ring.*  audio_pump.*
│       └── hdmi_audio*.{c,h}
└── tests/
    └── render_host/      host pixel tests for the dual-target TUs
```

## Notes / rough edges

- A few header entry points are declared here but implemented by the
  consumer because only one game needs them so far: the `Mix_*` mixer
  core (`shim_mixer.c`), and `SDL_mutex`/`SDL_cond`/`SDL_MixAudioFormat`
  (Doom's `opl_glue.c`, mapped to Zephyr `k_mutex`/`k_condvar`). The
  threading + mix helpers are game-agnostic and are good candidates to
  promote into the lib as more consumers land.
- Some source-file header comments still say "SDLPoP", cosmetic, from
  before the neutralisation.
- `sdl2shim.h` still carries some SDLPoP-era seam declarations (surface,
  present, asset store) that a given app may or may not implement.
