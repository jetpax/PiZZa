# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- sokoban (howprice/sdl2-sokoban) via the shared SDL2 shim
# (apps/lib/sdl2shim).
#
# First consumer of the shim's SDL_Renderer engine + TTF-atlas + PIMG
# groups: the game is written against the renderer API proper (unlike
# Doom/SDLPoP/minivmac's app-side composition), so the lib owns the
# whole draw path and this app supplies only the diverging half --
# the baked asset pack + fd layer, the TMX loader over baked records,
# the present backend, the `sok` shell and the scripted timeline.
#
# The game tree is VENDORED at game/ (upstream is dormant; see
# game/PROVENANCE.md for the commit + local patches). MIT game, Apache
# shim/port glue.
#
# C++: the game is C++14-compatible C++; the proven aarch64 recipe is
# CONFIG_CPP + CONFIG_GLIBCXX_LIBCPP in prj.conf plus
# -DTOOLCHAIN_HAS_GLIBCXX=ON on the west line.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_sokoban LANGUAGES C CXX)

# sample_usbd helper for the USB CDC ACM shell, same as PizzaShell /
# Jet / wipeout / Doom / SDLPoP.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

set(SOK_GAME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/game)

set(SOK_GAME_SRC
  ${SOK_GAME_DIR}/src/main.cpp
  ${SOK_GAME_DIR}/src/App.cpp
  ${SOK_GAME_DIR}/src/Game.cpp
  ${SOK_GAME_DIR}/src/MapRender.cpp
  ${SOK_GAME_DIR}/src/Renderer.cpp
  ${SOK_GAME_DIR}/src/TileMap.cpp
  ${SOK_GAME_DIR}/src/hp_assert.cpp
)

# ── shared shim (apps/lib/sdl2shim) ─────────────────────────────────
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

set(SOK_SHIM_SRC
  src/shim_main.c
  src/shim_tmx.c
  src/sok_assets.c
  src/sok_pack.S
  ${SDL2SHIM_CORE_SOURCES}
  ${SDL2SHIM_SURFACE_SOURCES}
  ${SDL2SHIM_RW_SOURCES}
  ${SDL2SHIM_RENDER_SOURCES}
  ${SDL2SHIM_TTF_ATLAS_SOURCES}
  ${SDL2SHIM_IMAGE_PIMG_SOURCES}
)

# Audio backend (lib-owned): sokoban makes no audio calls at all, but
# the core's shim_stub.c delegates to the s2s_audio_* seam, so a
# backend TU must link. NONE stays correct even if a future game
# variant opens audio.
if(CONFIG_SDL2SHIM_AUDIO_HDMI)
  list(APPEND SOK_SHIM_SRC ${SDL2SHIM_AUDIO_HDMI_SOURCES})
else()
  list(APPEND SOK_SHIM_SRC ${SDL2SHIM_AUDIO_NONE})
endif()

# `sok` shell command set -- interactive key injection over USB CDC ACM.
if(CONFIG_SHELL)
  list(APPEND SOK_SHIM_SRC src/shim_shell.c)
endif()

# USB device stack bring-up (lib).
if(CONFIG_USB_DEVICE_STACK_NEXT)
  list(APPEND SOK_SHIM_SRC ${SDL2SHIM_USB})
endif()

# Frame present backend (app-local, as for every shim consumer).
if(CONFIG_SOKOBAN_PRESENT_DISPLAY)
  list(APPEND SOK_SHIM_SRC src/sok_present_display.c)
elseif(CONFIG_SOKOBAN_PRESENT_SEMIHOST)
  list(APPEND SOK_SHIM_SRC src/sok_present_semihost.c)
else()
  list(APPEND SOK_SHIM_SRC src/sok_present_none.c)
endif()

# Scripted synthetic input -- sokoban's title -> level 1 -> moves
# timeline for the qemu sim gate.
if(CONFIG_SOKOBAN_SCRIPTED_INPUT)
  list(APPEND SOK_SHIM_SRC src/shim_scripted.c)
endif()

# BT HID input (pad/keyboard/mouse) via apps/lib/btinput -- rpi_zero_2w
# only (boards/rpi_zero_2w.conf sets CONFIG_BTINPUT; qemu has no radio).
if(CONFIG_BTINPUT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/btinput/btinput.cmake)
  # The BT patchram (.hcd) is a gitignored vendor blob (PROVENANCE.md).
  # Fail LOUDLY if missing: an empty-patchram image wedges the controller
  # at runtime (Download_Minidriver with nothing to launch).
  if(NOT CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB)
    message(FATAL_ERROR
      "sokoban+BTINPUT: CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB is unset")
  endif()
  set(_bt_blob "${CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB}")
  if(NOT IS_ABSOLUTE "${_bt_blob}")
    set(_bt_blob "${CMAKE_CURRENT_SOURCE_DIR}/${_bt_blob}")
  endif()
  if(NOT EXISTS "${_bt_blob}")
    message(FATAL_ERROR
      "sokoban+BTINPUT: BT patchram not found:\n  ${_bt_blob}\n"
      "It is gitignored (vendor blob) -- fetch it per "
      "apps/lib/btinput/firmware/PROVENANCE.md before building.")
  endif()
  target_sources(app PRIVATE ${BTINPUT_SOURCES} src/sokoban_btinput.c)
  target_include_directories(app PRIVATE ${BTINPUT_INCLUDE_DIRS})
endif()

# Embedded asset pack: pack_assets.py bakes PNG -> PIMG, TMX -> S2TM
# and the ARCADE_N 8 px glyph atlas into one binary, linked via .incbin.
set(SOK_PACK_BIN ${CMAKE_BINARY_DIR}/sokoban_assets.bin)
file(GLOB_RECURSE SOK_DATA_FILES ${SOK_GAME_DIR}/data/*)
add_custom_command(
  OUTPUT ${SOK_PACK_BIN}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_assets.py
          ${SOK_GAME_DIR}/data ${SOK_PACK_BIN} --font "Font/ARCADE_N.TTF@8"
  DEPENDS ${SOK_DATA_FILES}
          ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_assets.py
  COMMENT "Packing sokoban assets (PNG -> PIMG, TMX -> S2TM, TTF -> S2SA)"
)
add_custom_target(sok_assets_pack DEPENDS ${SOK_PACK_BIN})
add_dependencies(app sok_assets_pack)
set_source_files_properties(src/sok_pack.S PROPERTIES
  OBJECT_DEPENDS ${SOK_PACK_BIN}
  COMPILE_OPTIONS "-I${CMAKE_BINARY_DIR};-Wa,-I${CMAKE_BINARY_DIR}"
)

target_sources(app PRIVATE
  ${SOK_GAME_SRC}
  ${SOK_SHIM_SRC}
)

# include/ order: the shim's include/ first so <SDL2/SDL.h> resolves
# there; the game includes "SDL.h"/"SDL_ttf.h"/"tmx.h" unqualified, so
# the shim's SDL2/ subdir and this app's include/ (tmx.h) are on the
# path directly.
target_include_directories(app PRIVATE
  ${SDL2SHIM_INCLUDE_DIRS}
  ${SDL2SHIM_DIR}/include/SDL2
  include
  src
)

# The vendored game trips warnings its desktop builds silence.
set_source_files_properties(${SOK_GAME_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-parameter;-Wno-missing-field-initializers;-Wno-format-truncation;-Wno-double-promotion"
)
