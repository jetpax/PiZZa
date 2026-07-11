# SPDX-License-Identifier: Apache-2.0
#
# apps/lib/sdl2shim -- helper for consuming Zephyr apps.
#
# The shared, game-agnostic half of the SDL2 shim: init/error/window,
# event queue + keyboard, timers, the audio/joystick/haptic delegation
# stubs, and the read-only POSIX odds-and-ends. The video path, the
# frame present backend, the asset store and (for now) the SDL_mixer
# implementation stay in each app because they diverge by frame format,
# asset layout and board.
#
# Usage from an app CMakeLists.txt:
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)
#   target_sources(app PRIVATE ${SDL2SHIM_CORE_SOURCES} ${SDL2SHIM_AUDIO_NONE})
#   target_include_directories(app PRIVATE ${SDL2SHIM_INCLUDE_DIRS})

set(SDL2SHIM_DIR ${CMAKE_CURRENT_LIST_DIR})

set(SDL2SHIM_CORE_SOURCES
  ${SDL2SHIM_DIR}/src/shim_core.c
  ${SDL2SHIM_DIR}/src/shim_event.c
  ${SDL2SHIM_DIR}/src/shim_timer.c
  ${SDL2SHIM_DIR}/src/shim_stub.c
  ${SDL2SHIM_DIR}/src/shim_posix.c
)

# Opt-in pieces the app appends as needed.
set(SDL2SHIM_AUDIO_NONE ${SDL2SHIM_DIR}/src/audio_none.c)
set(SDL2SHIM_USB        ${SDL2SHIM_DIR}/src/shim_usb.c)

# Software surfaces/palettes/blits (promoted from SDLPoP). Required by
# the render, TTF and PIMG groups below; apps with their own surface
# implementation (none today) simply don't opt in.
set(SDL2SHIM_SURFACE_SOURCES ${SDL2SHIM_DIR}/src/shim_surface.c)

# Software SDL_Renderer/SDL_Texture engine over the s2s_present seam
# (sokoban; any SDL_Renderer-API game). Requires SURFACE.
set(SDL2SHIM_RENDER_SOURCES ${SDL2SHIM_DIR}/src/shim_render.c)

# SDL2_ttf over baked S2SA glyph atlases (sdl2shim_atlas.h). Requires
# SURFACE and an app asset store implementing s2s_asset_find.
set(SDL2SHIM_TTF_ATLAS_SOURCES ${SDL2SHIM_DIR}/src/shim_ttf_atlas.c)

# SDL2_image over pre-baked PIMG blobs (promoted from SDLPoP). Requires
# SURFACE, RW and s2s_asset_find.
set(SDL2SHIM_IMAGE_PIMG_SOURCES ${SDL2SHIM_DIR}/src/shim_image_pimg.c)

# SDL_RWops, memory-backed (promoted from SDLPoP; RWFromFile fails
# clean -- consumers use their pack fd layer for files).
set(SDL2SHIM_RW_SOURCES ${SDL2SHIM_DIR}/src/shim_rw.c)

# HDMI-MAI audio backend: the freestanding IEC958/resample/ring/pump
# core (host-tested) + MAI programming + the SDL-audio-callback glue.
set(SDL2SHIM_AUDIO_HDMI_SOURCES
  ${SDL2SHIM_DIR}/src/shim_audio_hdmi.c
  ${SDL2SHIM_DIR}/src/audio/iec958_pack.c
  ${SDL2SHIM_DIR}/src/audio/resample_48k.c
  ${SDL2SHIM_DIR}/src/audio/ring.c
  ${SDL2SHIM_DIR}/src/audio/audio_pump.c
  ${SDL2SHIM_DIR}/src/audio/hdmi_audio_init.c
)

set(SDL2SHIM_INCLUDE_DIRS
  ${SDL2SHIM_DIR}/include
  ${SDL2SHIM_DIR}
  ${SDL2SHIM_DIR}/src
)
