<!-- SPDX-License-Identifier: Apache-2.0 -->
# sdl2shim

A minimal SDL2 / SDL_mixer / SDL_image / SDL_ttf shim for Zephyr.
Enough of the APIs for retro C and C++ games (Doom, SDLPoP, Mini vMac,
DOSBox, sokoban) to build and run on Zephyr with no real SDL, no OS,
and no windowing stack. Game code keeps its `#include <SDL2/SDL.h>`
etc. and links against this shim unmodified. Games written against the
`SDL_Renderer` API proper get an opt-in software renderer engine;
images and fonts come from build-time-baked assets (PIMG blobs, S2SA
glyph atlases) -- no decoders on the device.

Consumers: [`apps/Doom`](../../Doom), [`apps/SDLPoP`](../../SDLPoP),
[`apps/minivmac`](../../minivmac), [`apps/DOSBox`](../../DOSBox), and
[`apps/sokoban`](../../sokoban) (the renderer-engine worked example).

## Consuming it

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

target_sources(app PRIVATE ${SDL2SHIM_CORE_SOURCES})
target_include_directories(app PRIVATE ${SDL2SHIM_INCLUDE_DIRS})

# pick an audio backend + any opt-in pieces:
#   ${SDL2SHIM_AUDIO_NONE}   or  ${SDL2SHIM_AUDIO_HDMI_SOURCES}
#   ${SDL2SHIM_USB}              # CDC-ACM shell
#   ${SDL2SHIM_SURFACE_SOURCES}  # software surfaces/blits
#   ${SDL2SHIM_RW_SOURCES}       # memory RWops
#   ${SDL2SHIM_RENDER_SOURCES}   # SDL_Renderer/SDL_Texture engine
#   ${SDL2SHIM_TTF_ATLAS_SOURCES} # SDL_ttf over baked glyph atlases
#   ${SDL2SHIM_IMAGE_PIMG_SOURCES} # SDL_image over baked PIMG blobs
```

... and `rsource "../lib/sdl2shim/Kconfig"` from the app Kconfig. The
shim's `include/` must precede the game's own include dirs so
`<SDL2/SDL.h>` resolves here. [`apps/Doom/CMakeLists.txt`](../../Doom)
is the worked example for app-side composition,
[`apps/sokoban/CMakeLists.txt`](../../sokoban) for the renderer engine
+ baked TTF/image assets.

See [`NOTES.md`](NOTES.md) for what the shim provides, what it
deliberately does not, the lib/app split, and layout.
