<!-- SPDX-License-Identifier: Apache-2.0 -->
# sdl2shim — a minimal SDL2 / SDL_mixer shim for Zephyr

A small, freestanding implementation of the **subset** of SDL2 and SDL_mixer
that retro C games actually call, so they build and run on Zephyr with no real
SDL, no operating system, and no windowing stack. Game code keeps its
`#include <SDL2/SDL.h>` / `<SDL2/SDL_mixer.h>` and links against this shim
unmodified.

It is a **compatibility shim, not a port of SDL.** The goal is "enough SDL to
run *this* game", grown one symbol at a time as consumers need them.

- **Prefix / config:** public headers live under `include/SDL2/`; internal
  cross-TU seams use the `s2s_` prefix and `CONFIG_SDL2SHIM_*` Kconfig.
- **Consumers:** `apps/Doom` (sdl2-doom) today — video + input + SFX + OPL
  music, hardware-verified on rpi_zero_2w. The SDLPoP port is the *origin*
  these TUs were neutralised from (`pop_`→`s2s_`) and is the next thing to
  migrate onto the lib.

## What it provides (compiled into the lib)

Game-agnostic translation units, grouped as the consuming CMake pulls them:

- **Core** (`SDL2SHIM_CORE_SOURCES`, always linked)
  - `shim_core.c` — init/quit, error string, version/hints, the single
    implicit "window", messagebox/iconv/name helpers.
  - `shim_event.c` — the SDL event queue + `SDL_GetKeyboardState` array, and
    the **producer seam** (`s2s_event_submit`) any input source feeds
    (scripted input, a shell command, a future BT-keyboard thread). Derives
    `keysym.sym` from the scancode.
  - `shim_timer.c` — `SDL_GetTicks`/`Delay`, perf counters, `SDL_AddTimer`
    (via `k_timer`, callbacks in ISR context).
  - `shim_stub.c` — routes the SDL audio entry points to the backend seam,
    plus no-op joystick / game-controller / haptic accessors (no pads exist).
  - `shim_posix.c` — the POSIX odds-and-ends a hosted libc would supply.

- **Audio backend** (Kconfig `choice`, one of)
  - `SDL2SHIM_AUDIO_NONE` (`audio_none.c`) — `s2s_audio_open` fails; the game
    runs silent. Used on qemu / boards with no audio.
  - `SDL2SHIM_AUDIO_HDMI` (`shim_audio_hdmi.c` + `src/audio/`) — real sound
    out of HDMI via the VC4 MAI: the game's 44.1 kHz callback mix is
    resampled to 48 kHz, IEC958-packed, and DMA-fed (DREQ-paced) into the MAI
    FIFO. The freestanding, host-tested core is `src/audio/` (resample, IEC958
    pack, cyclic ring, feeder pump, MAI/clock setup). `+HDMI_TEST_TONE` boots
    a 1 kHz tone for hardware sign-off.

- **Opt-in helpers**
  - `SDL2SHIM_SCRIPTED` (`shim_scripted.c`) — a synthetic input timeline
    (SDLPoP's; Doom brings its own app-local one).
  - `SDL2SHIM_USB` (`shim_usb.c`) — USB CDC-ACM shell bring-up for a device
    console on the rpi_zero_2w.

- **Headers** (`include/`)
  - `SDL2/SDL.h` — the SDL2 API subset: init/error, the single window +
    drawable size, event queue + keyboard, timers + perf counters, the audio
    device seam, RWops, threading (`SDL_mutex`/`SDL_cond`), `SDL_MixAudioFormat`,
    and stubbed joystick/controller/haptic types.
  - `SDL2/SDL_mixer.h` — the SDL_mixer subset: the SFX `Mix_*` API, stubbed
    music entry points, and the `Mix_RegisterEffect`/`MIX_CHANNEL_POST` hook
    OPL music plugs into.
  - `SDL2/SDL_endian.h` — `SDL_SwapLE16/32` + endian constants (Doom's
    `i_swap.h` needs them).
  - `dirent.h` — picolibc has none; a minimal directory-enumeration stub.

## What it does NOT do (by design)

- **It is not real SDL.** Only the calls the current consumers make exist. New
  symbols get added as new games need them — do not expect the full API.
- **No rendering, no surface/texture engine, no frame present.** There is one
  implicit "window"; the actual frame composition + present backend live in
  the *consuming app*, because the frame format diverges (SDLPoP blits 8-bit
  surfaces; Doom pushes a native 320×200 XRGB buffer).
- **The SDL_mixer `Mix_*` implementation is the app's** (`shim_mixer.c`), not
  the lib's. Mixing, sample rate, and effect use diverge per game. The lib
  owns the *header* and the *audio backend the mixer feeds*, not the mixer core.
- **No real filesystem.** A consumer supplies a thin read-only fd layer over
  its embedded asset pack (Doom's WAD, SDLPoP's data); disk writes fail EROFS
  (a game that needs a scratch file backs one path with RAM). `dirent` only
  enumerates that.
- **`main()` belongs to the app.** The game's `main` is renamed to `SDL_main`;
  the app's `shim_main.c` is the Zephyr entry that calls it.
- **No image/font/net/thread-pool loaders, no software scaler, no SDL_ttf /
  SDL_image**, etc. — none of that is here.

## The lib / app split

The neutral, reusable half is here; the half that diverges per game stays in
the consuming app:

| Concern                                   | Where     |
| ----------------------------------------- | --------- |
| init, error, events, timers, POSIX        | **lib** (core) |
| audio backend (none / HDMI-MAI)           | **lib**   |
| SDL_mixer `Mix_*` implementation          | app       |
| video compose + frame present backend     | app       |
| asset store (fd layer over the pack)      | app       |
| `main()` / Zephyr entry                   | app       |

## Using it from a consumer

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

target_sources(app PRIVATE ${SDL2SHIM_CORE_SOURCES})
target_include_directories(app PRIVATE ${SDL2SHIM_INCLUDE_DIRS})

# pick an audio backend + any opt-in pieces:
#   ${SDL2SHIM_AUDIO_NONE}   or  ${SDL2SHIM_AUDIO_HDMI_SOURCES}
#   ${SDL2SHIM_SCRIPTED}         # synthetic input
#   ${SDL2SHIM_USB}              # CDC-ACM shell
```

…and `rsource "../lib/sdl2shim/Kconfig"` from the app Kconfig. The shim's
`include/` must precede the game's own include dirs so `<SDL2/SDL.h>` resolves
here. `apps/Doom/CMakeLists.txt` is the worked example.

## Layout

```
sdl2shim/
├── README.md
├── Kconfig               # audio-backend choice + log level
├── sdl2shim.cmake        # source groups + include dirs for consumers
├── sdl2shim.h            # INTERNAL seams (s2s_*), not seen by the game
├── include/
│   ├── SDL2/SDL.h        # SDL2 subset
│   ├── SDL2/SDL_mixer.h  # SDL_mixer subset
│   ├── SDL2/SDL_endian.h
│   └── dirent.h
└── src/
    ├── shim_core.c  shim_event.c  shim_timer.c  shim_stub.c  shim_posix.c
    ├── shim_scripted.c   shim_usb.c
    ├── audio_none.c      shim_audio_hdmi.c
    └── audio/            # freestanding HDMI-MAI core (host-tested):
        ├── resample_48k.*  iec958_pack.*  ring.*  audio_pump.*
        └── hdmi_audio*.{c,h}
```

## Notes / rough edges

- A few header entry points are **declared here but implemented by the
  consumer** because only one game needs them so far: the `Mix_*` mixer core
  (`shim_mixer.c`), and `SDL_mutex`/`SDL_cond`/`SDL_MixAudioFormat` (Doom's
  `opl_glue.c`, mapped to Zephyr `k_mutex`/`k_condvar`). The threading + mix
  helpers are game-agnostic and are good candidates to promote into the lib
  when SDLPoP migrates.
- Some source-file header comments still say "SDLPoP" — cosmetic, from before
  the neutralisation.
- `sdl2shim.h` still carries some SDLPoP-era seam declarations (surface,
  present, asset store) that a given app may or may not implement.
