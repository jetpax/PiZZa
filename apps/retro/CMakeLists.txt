# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- libretro frontend (apps/retro).
#
# A nanoarch-class libretro frontend over the sdl2shim seams: the 6
# libretro callbacks map onto s2s_present_* / s2s_audio_* / the shim
# event queue, and the core is (for M0) statically linked into the
# image -- the RetroArch console fused-binary model. llext-loaded
# cores arrive at M2.
#
# Core: libretro-2048 (public domain), consumed as an untouched
# upstream tree via the LIBRETRO_2048_SRC_DIR env, like minivmac's
# MINIVMAC_SRC_DIR. Pure C -- no C++ toolchain flags needed.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_retro LANGUAGES C)

# sample_usbd helper for the USB CDC ACM shell on hardware boards.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

if(NOT DEFINED ENV{LIBRETRO_2048_SRC_DIR})
  message(FATAL_ERROR
    "retro: set LIBRETRO_2048_SRC_DIR to a libretro-2048 checkout "
    "(git clone https://github.com/libretro/libretro-2048)")
endif()
set(CORE_DIR $ENV{LIBRETRO_2048_SRC_DIR})

# Core source (Kconfig choice):
#  - STATIC: the 3 core TUs compile into the image (fused binary).
#  - LLEXT_EMBED / LLEXT_FS: the core is a separate 2048.llext built
#    by core2048/ (west build -t core2048); the image carries only the
#    binder + symbol exports. EMBED additionally .incbin's the .llext
#    (RETRO_CORE_LLEXT env names the file) for boards with no SD.
set(RETRO_CORE_SRC)
if(CONFIG_RETRO_CORE_STATIC)
  # The non-cairo software renderer build: 3 TUs, XRGB8888.
  set(RETRO_CORE_SRC
    ${CORE_DIR}/libretro.c
    ${CORE_DIR}/game_noncairo.c
    ${CORE_DIR}/game_shared.c
  )
endif()

# ── shared shim (apps/lib/sdl2shim): CORE group only ────────────────
# The frontend makes no SDL draw calls; it consumes the event queue
# (SDL_PollEvent/SDL_GetKeyboardState) so every existing input
# producer (scripted, CDC shell, btinput) works unchanged.
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

set(RETRO_SRC
  src/main.c
  src/retro_frontend.c
  src/retro_vfs_stubs.c
  ${SDL2SHIM_CORE_SOURCES}
)

if(CONFIG_RETRO_CORE_STATIC)
  list(APPEND RETRO_SRC src/retro_core_static.c)
else()
  list(APPEND RETRO_SRC src/retro_core_llext.c src/retro_llext_exports.c)
endif()

if(CONFIG_RETRO_CORE_LLEXT_EMBED)
  if(NOT DEFINED ENV{RETRO_CORE_LLEXT})
    message(FATAL_ERROR
      "retro: RETRO_CORE_LLEXT_EMBED needs the RETRO_CORE_LLEXT env "
      "pointing at a built .llext (west build -t core2048 in core2048/)")
  endif()
  list(APPEND RETRO_SRC src/retro_core_pack.S)
  set_source_files_properties(src/retro_core_pack.S PROPERTIES
    OBJECT_DEPENDS $ENV{RETRO_CORE_LLEXT}
    COMPILE_DEFINITIONS "RETRO_CORE_LLEXT_PATH=\"$ENV{RETRO_CORE_LLEXT}\""
  )
endif()

# Audio backend (lib-owned). 2048 emits no audio; shim_stub.c still
# delegates to the s2s_audio_* seam, so a backend TU must link.
if(CONFIG_SDL2SHIM_AUDIO_HDMI)
  list(APPEND RETRO_SRC ${SDL2SHIM_AUDIO_HDMI_SOURCES})
else()
  list(APPEND RETRO_SRC ${SDL2SHIM_AUDIO_NONE})
endif()

# USB device stack bring-up (lib).
if(CONFIG_USB_DEVICE_STACK_NEXT)
  list(APPEND RETRO_SRC ${SDL2SHIM_USB})
endif()

# `retro` shell command set -- interactive retropad injection over CDC.
if(CONFIG_SHELL)
  list(APPEND RETRO_SRC src/retro_shell.c)
endif()

# Frame present backend (app-local, as for every shim consumer).
if(CONFIG_RETRO_PRESENT_DISPLAY)
  list(APPEND RETRO_SRC src/retro_present_display.c)
elseif(CONFIG_RETRO_PRESENT_SEMIHOST)
  list(APPEND RETRO_SRC src/retro_present_semihost.c)
else()
  list(APPEND RETRO_SRC src/retro_present_none.c)
endif()

# Scripted synthetic input -- title -> game -> moves timeline for the
# qemu sim gate.
if(CONFIG_RETRO_SCRIPTED_INPUT)
  list(APPEND RETRO_SRC src/retro_scripted.c)
endif()

# BT HID input (pad/keyboard) via apps/lib/btinput -- rpi_zero_2w only
# (boards/rpi_zero_2w.conf sets CONFIG_BTINPUT; qemu has no radio).
if(CONFIG_BTINPUT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/btinput/btinput.cmake)
  # The BT patchram (.hcd) is a gitignored vendor blob (PROVENANCE.md).
  # Fail LOUDLY if missing: an empty-patchram image wedges the controller
  # at runtime (Download_Minidriver with nothing to launch).
  if(NOT CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB)
    message(FATAL_ERROR
      "retro+BTINPUT: CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB is unset")
  endif()
  set(_bt_blob "${CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB}")
  if(NOT IS_ABSOLUTE "${_bt_blob}")
    set(_bt_blob "${CMAKE_CURRENT_SOURCE_DIR}/${_bt_blob}")
  endif()
  if(NOT EXISTS "${_bt_blob}")
    message(FATAL_ERROR
      "retro+BTINPUT: BT patchram not found:\n  ${_bt_blob}\n"
      "It is gitignored (vendor blob) -- fetch it per "
      "apps/lib/btinput/firmware/PROVENANCE.md before building.")
  endif()
  target_sources(app PRIVATE ${BTINPUT_SOURCES} src/retro_btinput.c)
  target_include_directories(app PRIVATE ${BTINPUT_INCLUDE_DIRS})
endif()

target_sources(app PRIVATE
  ${RETRO_CORE_SRC}
  ${RETRO_SRC}
)

target_include_directories(app PRIVATE
  ${SDL2SHIM_INCLUDE_DIRS}
  ${CORE_DIR}
  ${CORE_DIR}/libretro-common/include
  src
)

# The vendored core trips warnings its desktop builds silence.
set_source_files_properties(${RETRO_CORE_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wno-unused-variable;-Wno-unused-parameter;-Wno-missing-field-initializers;-Wno-double-promotion;-Wno-missing-prototypes;-Wno-unused-but-set-variable"
)
