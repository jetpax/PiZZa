# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- SDLPoP (Prince of Persia) via the shared minimal SDL2 shim
# (apps/lib/sdl2shim).
#
# Stand-alone Zephyr application linking the NagyD/SDLPoP game sources
# (pure C, UNTOUCHED -- the tree at ~/github/SDLPoP is never edited)
# against the shared SDL2 shim, landing on the proven Jet/wipeout
# framebuffer present path (bcm2835_fb + HVS 320x200 -> panel scaling).
#
# The game-agnostic half of the shim (init/event/timer/stub/posix, the
# audio backends, USB bring-up, the SDL/dirent headers) comes from the
# lib; the SDLPoP-specific half (surface/blit, RWops, IMG_Load, the video
# compose + present backends, the asset pack, the `pop` shell, the scripted
# timeline, main) stays here because it diverges by frame format + assets.
#
# SDLPoP is GPL-3.0-or-later; this binary is GPL-3.0 by virtue of linking
# it. The shim + port files themselves are Apache-2.0, same model as the
# wipeout demo (AGPL engine, Apache port glue).
#
# Asset pipeline decision (work-order §4, made in phase 1):
#   PREFERRED  build-time conversion -- res###.png -> raw indexed blobs
#              packed into an embedded asset image linked into the app;
#              IMG_Load_RW parses the raw header instead of PNG.
#   FALLBACK   SD + FAT (proven on PiZZa) if asset iteration matters.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza-sdlpop C)

# sample_usbd helper for the USB CDC ACM shell, same as PizzaShell /
# Jet / wipeout / Doom.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

if(NOT DEFINED POP_SRC_DIR)
  set(POP_SRC_DIR "$ENV{HOME}/github/SDLPoP/src")
endif()
if(NOT EXISTS "${POP_SRC_DIR}/seg000.c")
  message(FATAL_ERROR
    "SDLPoP sources not found at ${POP_SRC_DIR}. Clone "
    "https://github.com/NagyD/SDLPoP and either keep it at "
    "~/github/SDLPoP or pass -DPOP_SRC_DIR=<path>.")
endif()

# Upstream game sources, complete and unmodified (list mirrors the
# upstream CMakeLists). opl3/stb_vorbis are the in-tree audio generators --
# they compile everywhere and stay silent behind the stubbed audio backend.
set(POP_GAME_SRC
  ${POP_SRC_DIR}/main.c
  ${POP_SRC_DIR}/data.c
  ${POP_SRC_DIR}/lighting.c
  ${POP_SRC_DIR}/menu.c
  ${POP_SRC_DIR}/midi.c
  ${POP_SRC_DIR}/opl3.c
  ${POP_SRC_DIR}/options.c
  ${POP_SRC_DIR}/replay.c
  ${POP_SRC_DIR}/screenshot.c
  ${POP_SRC_DIR}/seg000.c
  ${POP_SRC_DIR}/seg001.c
  ${POP_SRC_DIR}/seg002.c
  ${POP_SRC_DIR}/seg003.c
  ${POP_SRC_DIR}/seg004.c
  ${POP_SRC_DIR}/seg005.c
  ${POP_SRC_DIR}/seg006.c
  ${POP_SRC_DIR}/seg007.c
  ${POP_SRC_DIR}/seg008.c
  ${POP_SRC_DIR}/seg009.c
  ${POP_SRC_DIR}/seqtbl.c
  ${POP_SRC_DIR}/stb_vorbis.c
)

# ── shared shim (apps/lib/sdl2shim) ─────────────────────────────────
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

# SDLPoP-specific shim TUs (diverge by frame format / assets / game):
set(POP_SHIM_SRC
  src/shim_main.c
  src/shim_video.c
  src/shim_surface.c
  src/shim_rw.c
  src/shim_image.c
  ${SDL2SHIM_CORE_SOURCES}
)

# Audio backend (lib-owned): "none" fails SDL_OpenAudio -> game disables
# audio; HDMI/MAI is the real §5a backend the game callback feeds.
if(CONFIG_SDL2SHIM_AUDIO_HDMI)
  list(APPEND POP_SHIM_SRC ${SDL2SHIM_AUDIO_HDMI_SOURCES})
else()
  list(APPEND POP_SHIM_SRC ${SDL2SHIM_AUDIO_NONE})
endif()

# `pop` shell command set -- interactive key injection over USB CDC ACM
# (the input path on hardware until HOGP keyboard lands). App-local.
if(CONFIG_SHELL)
  list(APPEND POP_SHIM_SRC src/shim_shell.c)
endif()

# USB device stack bring-up (lib). Required because the board conf defers
# CDC auto-init; without this the port never enumerates on the host.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  list(APPEND POP_SHIM_SRC ${SDL2SHIM_USB})
endif()

# Frame present backend (app-local -- diverges by frame format).
if(CONFIG_SDLPOP_PRESENT_DISPLAY)
  list(APPEND POP_SHIM_SRC src/pop_present_display.c)
elseif(CONFIG_SDLPOP_PRESENT_SEMIHOST)
  list(APPEND POP_SHIM_SRC src/pop_present_semihost.c)
else()
  list(APPEND POP_SHIM_SRC src/pop_present_none.c)
endif()

if(CONFIG_SDLPOP_VIDEO_SELFTEST)
  list(APPEND POP_SHIM_SRC src/shim_selftest.c)
endif()

# Scripted synthetic input -- SDLPoP's own Prince-of-Persia timeline,
# app-local like Doom's doom_scripted.c (the lib's is game-specific).
if(CONFIG_SDLPOP_SCRIPTED_INPUT)
  list(APPEND POP_SHIM_SRC src/shim_scripted.c)
endif()

# Embedded asset pack (§4 preferred pipeline): pack_assets.py pre-converts
# every PNG to a raw PIMG blob and packs the data/ tree (music excluded)
# into one binary, linked via .incbin.
if(NOT DEFINED POP_DATA_DIR)
  get_filename_component(POP_DATA_DIR "${POP_SRC_DIR}/../data" ABSOLUTE)
endif()
set(POP_PACK_BIN ${CMAKE_BINARY_DIR}/sdlpop_assets.bin)
file(GLOB_RECURSE POP_DATA_FILES ${POP_DATA_DIR}/*)
set(POP_PACK_OVERLAY_ARGS "")
set(POP_PACK_OVERLAY_DEPS "")
# A single SDLPoP.ini overlay is packed at the pack root. The sim's tuning
# ini (fades/music off, reach gameplay fast under TCG) takes precedence;
# otherwise the hardware ini disables the SDLPoP info screen so boot lands
# on the original intro.
if(CONFIG_SDLPOP_SIM_TUNING)
  set(POP_OVERLAY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sim_overlay)
elseif(CONFIG_SDLPOP_SKIP_INFO_SCREEN)
  set(POP_OVERLAY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hw_overlay)
endif()
if(DEFINED POP_OVERLAY_DIR)
  file(GLOB_RECURSE POP_OVERLAY_FILES ${POP_OVERLAY_DIR}/*)
  set(POP_PACK_OVERLAY_ARGS --overlay ${POP_OVERLAY_DIR})
  set(POP_PACK_OVERLAY_DEPS ${POP_OVERLAY_FILES})
endif()
add_custom_command(
  OUTPUT ${POP_PACK_BIN}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_assets.py
          ${POP_DATA_DIR} ${POP_PACK_BIN} ${POP_PACK_OVERLAY_ARGS}
  DEPENDS ${POP_DATA_FILES} ${POP_PACK_OVERLAY_DEPS}
          ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_assets.py
  COMMENT "Packing SDLPoP assets (PNG -> PIMG)"
)
add_custom_target(pop_assets_pack DEPENDS ${POP_PACK_BIN})
add_dependencies(app pop_assets_pack)

list(APPEND POP_SHIM_SRC src/pop_assets.c src/pop_pack.S)
set_source_files_properties(src/pop_pack.S PROPERTIES
  OBJECT_DEPENDS ${POP_PACK_BIN}
  COMPILE_OPTIONS "-I${CMAKE_BINARY_DIR};-Wa,-I${CMAKE_BINARY_DIR}"
)

target_sources(app PRIVATE
  ${POP_GAME_SRC}
  ${POP_SHIM_SRC}
)

# include/ order: the shared shim's SDL2 headers first so <SDL2/SDL.h>
# resolves there, then SDLPoP's include/ for <SDL2/SDL_image.h> (which the
# lib does not carry), then the game tree.
target_include_directories(app PRIVATE
  ${SDL2SHIM_INCLUDE_DIRS}
  include
  ${POP_SRC_DIR}
  src
)

# The game trips pedantic warnings upstream builds silence too.
# -include: SDLPoP assumes glibc, where <string.h>/<stdlib.h> pull in
# strcasecmp and alloca; picolibc keeps them in <strings.h>/<alloca.h>.
# -Didle=...: SDLPoP's global idle() collides with the Zephyr kernel
# idle-thread entry symbol -- the link silently resolved the kernel's idle
# thread to the GAME's event loop (crashed on first k_msleep). Rename in
# game TUs only, same trick as SDL's main=SDL_main.
set_source_files_properties(${POP_GAME_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-missing-braces;-Wno-unused-function;-Wno-maybe-uninitialized;-include;strings.h;-include;alloca.h;-D_DEFAULT_SOURCE;-Didle=pop_game_idle"
)
