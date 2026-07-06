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
# and, when the app enables scripted input:
#   target_sources(app PRIVATE ${SDL2SHIM_SCRIPTED})

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
set(SDL2SHIM_SCRIPTED   ${SDL2SHIM_DIR}/src/shim_scripted.c)

set(SDL2SHIM_INCLUDE_DIRS
  ${SDL2SHIM_DIR}/include
  ${SDL2SHIM_DIR}
)
