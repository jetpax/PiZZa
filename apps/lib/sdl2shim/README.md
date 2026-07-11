<!-- SPDX-License-Identifier: Apache-2.0 -->
# sdl2shim

A minimal SDL2 / SDL_mixer shim for Zephyr. Enough of the two APIs for
retro C games (Doom, SDLPoP, Mini vMac, DOSBox) to build and run on
Zephyr with no real SDL, no OS, and no windowing stack. Game code keeps
its `#include <SDL2/SDL.h>` / `<SDL2/SDL_mixer.h>` and links against
this shim unmodified.

Consumers: [`apps/Doom`](../../Doom), [`apps/SDLPoP`](../../SDLPoP),
[`apps/minivmac`](../../minivmac), and [`apps/DOSBox`](../../DOSBox).

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
is the worked example.

See [`NOTES.md`](NOTES.md) for what the shim provides, what it
deliberately does not, the lib/app split, and layout.
