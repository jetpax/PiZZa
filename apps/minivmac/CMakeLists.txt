# SPDX-License-Identifier: Apache-2.0
#
# PiZZa -- Mini vMac (Macintosh Plus) via the shared minimal SDL2 shim
# (apps/lib/sdl2shim). Third consumer after Doom + SDLPoP.
#
# Links the upstream Mini vMac 68K emulator core + its generated SDL2
# OS-glue (OSGLUSDL.c) against the shim. The tree at ~/github/minivmac is
# UNTOUCHED: src/ is pristine upstream, and the Mac-Plus/SDL2 variation is
# the generated cfg/ headers (setup_t -m Plus -api sd2). See the port memory.
#
# Mini vMac is GPL-2.0; this binary is GPL-2.0 by virtue of linking it. The
# shim + port glue are Apache-2.0, same model as the Doom + SDLPoP ports.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza-minivmac C ASM)

# sample_usbd helper for the USB CDC ACM shell (rpi_zero_2w), same as the
# Doom / SDLPoP / wipeout ports.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

# ── Mini vMac tree (untouched upstream + generated cfg/) ─────────────
if(NOT DEFINED MINIVMAC_SRC_DIR)
  set(MINIVMAC_SRC_DIR "$ENV{HOME}/github/minivmac")
endif()
if(NOT EXISTS "${MINIVMAC_SRC_DIR}/src/OSGLUSDL.c")
  message(FATAL_ERROR
    "Mini vMac sources not found at ${MINIVMAC_SRC_DIR}. Clone "
    "https://github.com/minivmac/minivmac and keep it at ~/github/minivmac "
    "or pass -DMINIVMAC_SRC_DIR=<path>.")
endif()
if(NOT EXISTS "${MINIVMAC_SRC_DIR}/cfg/CNFUIOSG.h")
  message(FATAL_ERROR
    "Mini vMac Mac-Plus/SDL2 variation not generated in ${MINIVMAC_SRC_DIR}. Run:\n"
    "  cd ${MINIVMAC_SRC_DIR} && gcc -o setup_t setup/tool.c && \\\n"
    "  ./setup_t -e bgc -t larm -cpu a64 -m Plus -api sd2 -sound 1 > setup.sh && bash setup.sh")
endif()

# The generated Makefile's TU list for -m Plus -api sd2: the 68K core +
# device models + the SDL2 OS-glue. Untouched upstream.
set(MINIVMAC_SRC
  ${MINIVMAC_SRC_DIR}/src/OSGLUSDL.c
  ${MINIVMAC_SRC_DIR}/src/GLOBGLUE.c
  ${MINIVMAC_SRC_DIR}/src/M68KITAB.c
  ${MINIVMAC_SRC_DIR}/src/MINEM68K.c
  ${MINIVMAC_SRC_DIR}/src/VIAEMDEV.c
  ${MINIVMAC_SRC_DIR}/src/IWMEMDEV.c
  ${MINIVMAC_SRC_DIR}/src/SCCEMDEV.c
  ${MINIVMAC_SRC_DIR}/src/RTCEMDEV.c
  ${MINIVMAC_SRC_DIR}/src/ROMEMDEV.c
  ${MINIVMAC_SRC_DIR}/src/SCSIEMDV.c
  ${MINIVMAC_SRC_DIR}/src/SONYEMDV.c
  ${MINIVMAC_SRC_DIR}/src/SCRNEMDV.c
  ${MINIVMAC_SRC_DIR}/src/MOUSEMDV.c
  ${MINIVMAC_SRC_DIR}/src/KBRDEMDV.c
  ${MINIVMAC_SRC_DIR}/src/SNDEMDEV.c
  ${MINIVMAC_SRC_DIR}/src/PROGMAIN.c
)

# ── shim ─────────────────────────────────────────────────────────────
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/sdl2shim/sdl2shim.cmake)

set(MINIVMAC_SHIM_SRC
  src/shim_main.c
  src/minivmac_video.c
  src/minivmac_assets.c
  src/minivmac_pack.S
)

if(CONFIG_MINIVMAC_PRESENT_DISPLAY)
  list(APPEND MINIVMAC_SHIM_SRC src/minivmac_present_display.c)
elseif(CONFIG_MINIVMAC_PRESENT_SEMIHOST)
  list(APPEND MINIVMAC_SHIM_SRC src/minivmac_present_semihost.c)
else()
  list(APPEND MINIVMAC_SHIM_SRC src/minivmac_present_none.c)
endif()

# Audio backend (lib-owned choice): qemu uses "none" (Mac boots silent);
# rpi_zero_2w uses the HDMI/MAI backend later (Phase 7).
if(CONFIG_SDL2SHIM_AUDIO_HDMI)
  list(APPEND MINIVMAC_SHIM_SRC ${SDL2SHIM_AUDIO_HDMI_SOURCES})
else()
  list(APPEND MINIVMAC_SHIM_SRC ${SDL2SHIM_AUDIO_NONE})
endif()

# USB device bring-up + the `minivmac` shell = interactive input on
# hardware (key + mouse), wired at Phase 4/5.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  list(APPEND MINIVMAC_SHIM_SRC ${SDL2SHIM_USB})
endif()

# The `minivmac` shell (key + mouse/click) = interactive input on hardware.
if(CONFIG_SHELL)
  list(APPEND MINIVMAC_SHIM_SRC src/minivmac_shell.c)
endif()

# Scripted synthetic input drives the Finder for the qemu sim gate.
if(CONFIG_MINIVMAC_SCRIPTED_INPUT)
  list(APPEND MINIVMAC_SHIM_SRC src/minivmac_scripted.c)
endif()

# Persistent disk backing on a mounted FATFS volume (SD card / RAM-FAT).
if(CONFIG_MINIVMAC_DISK_FS)
  list(APPEND MINIVMAC_SHIM_SRC src/minivmac_storage.c)
endif()

# BT HID input (keyboard + mouse) via apps/lib/btinput -- rpi_zero_2w only
# (boards/rpi_zero_2w.conf sets CONFIG_BTINPUT; qemu has no radio).
if(CONFIG_BTINPUT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/btinput/btinput.cmake)
  # The BT patchram (.hcd) is a gitignored vendor blob (PROVENANCE.md).
  # Fail LOUDLY if missing: an empty-patchram image wedges the controller
  # at runtime (Download_Minidriver with nothing to launch).
  if(NOT CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB)
    message(FATAL_ERROR
      "minivmac+BTINPUT: CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB is unset")
  endif()
  set(_bt_blob "${CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB}")
  if(NOT IS_ABSOLUTE "${_bt_blob}")
    set(_bt_blob "${CMAKE_CURRENT_SOURCE_DIR}/${_bt_blob}")
  endif()
  if(NOT EXISTS "${_bt_blob}")
    message(FATAL_ERROR
      "minivmac+BTINPUT: BT patchram not found:\n  ${_bt_blob}\n"
      "It is gitignored (vendor blob) -- fetch it per "
      "apps/lib/btinput/firmware/PROVENANCE.md before building.")
  endif()
  list(APPEND MINIVMAC_SHIM_SRC ${BTINPUT_SOURCES} src/minivmac_btinput.c)
endif()

# ── ROM bake (§5): raw .incbin into rodata ──────────────────────────
if(NOT DEFINED MINIVMAC_ROM)
  set(MINIVMAC_ROM "${MINIVMAC_SRC_DIR}/extras/roms/vMac.ROM")
endif()
if(NOT EXISTS "${MINIVMAC_ROM}")
  message(FATAL_ERROR "Mac Plus ROM not found: ${MINIVMAC_ROM}. Pass -DMINIVMAC_ROM=<path/to/vMac.ROM>.")
endif()
set(MINIVMAC_ROM_BIN ${CMAKE_BINARY_DIR}/minivmac_rom.bin)
add_custom_command(
  OUTPUT ${MINIVMAC_ROM_BIN}
  COMMAND ${CMAKE_COMMAND} -E copy ${MINIVMAC_ROM} ${MINIVMAC_ROM_BIN}
  DEPENDS ${MINIVMAC_ROM}
  COMMENT "Baking Mac Plus ROM ${MINIVMAC_ROM} into rodata"
)
add_custom_target(minivmac_rom_bake DEPENDS ${MINIVMAC_ROM_BIN})
add_dependencies(app minivmac_rom_bake)

# ── Boot disk bake (Phase 3): a bootable System 6.0.8 floppy into rodata,
# served through a writable RAM overlay (minivmac_assets.c). Default is the
# Startup floppy shipped in the pinned tree; the 100 MB HD image cannot be
# RAM-backed (SD/FAT persistence is a later phase).
if(NOT DEFINED MINIVMAC_DISK)
  set(MINIVMAC_DISK "${MINIVMAC_SRC_DIR}/extras/disks/608/Z-690-5830-B (Startup).img")
endif()
if(NOT EXISTS "${MINIVMAC_DISK}")
  message(FATAL_ERROR "Boot disk not found: ${MINIVMAC_DISK}. Pass -DMINIVMAC_DISK=<path/to/bootable.dsk>.")
endif()
set(MINIVMAC_DISK_BIN ${CMAKE_BINARY_DIR}/minivmac_disk.dsk)
add_custom_command(
  OUTPUT ${MINIVMAC_DISK_BIN}
  COMMAND ${CMAKE_COMMAND} -E copy "${MINIVMAC_DISK}" ${MINIVMAC_DISK_BIN}
  DEPENDS "${MINIVMAC_DISK}"
  COMMENT "Baking boot disk into rodata"
  VERBATIM
)
add_custom_target(minivmac_disk_bake DEPENDS ${MINIVMAC_DISK_BIN})
add_dependencies(app minivmac_disk_bake)

set_source_files_properties(src/minivmac_pack.S PROPERTIES
  OBJECT_DEPENDS "${MINIVMAC_ROM_BIN};${MINIVMAC_DISK_BIN}"
  COMPILE_OPTIONS "-I${CMAKE_BINARY_DIR};-Wa,-I${CMAKE_BINARY_DIR}"
)

# ── target ───────────────────────────────────────────────────────────
target_sources(app PRIVATE
  ${MINIVMAC_SRC}
  ${MINIVMAC_SHIM_SRC}
  ${SDL2SHIM_CORE_SOURCES}
)

# include order: shim SDL2 headers first so <SDL2/SDL.h> (pulled by
# cfg/CNFUIOSG.h) resolves to the shim, then the Mini vMac variation cfg/
# and the pristine upstream src/.
target_include_directories(app PRIVATE
  ${SDL2SHIM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${MINIVMAC_SRC_DIR}/cfg
  ${MINIVMAC_SRC_DIR}/src
)
if(CONFIG_BTINPUT)
  target_include_directories(app PRIVATE ${BTINPUT_INCLUDE_DIRS})
endif()

# Mini vMac TUs are pristine upstream ANSI C; silence the pedantic warnings
# its own build (and picolibc's stricter headers) would otherwise raise.
set_source_files_properties(${MINIVMAC_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-missing-braces;-Wno-implicit-fallthrough;-Wno-format;-Wno-sign-compare;-D_DEFAULT_SOURCE"
)
