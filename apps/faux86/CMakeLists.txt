# SPDX-License-Identifier: GPL-2.0-or-later
#
# PiZZa -- Faux86 (ArnoldUK/Faux86-remake) 8086 PC emulator on Zephyr.
#
# Faux86 has its own 5-method HostSystemInterface, so it does NOT use the
# SDL2 shim: the core links against a Zephyr host impl (src/faux86_host.cpp)
# that drives the app-local present seam + Zephyr timer directly.
#
# The upstream checkout stays pristine; the core src/ is gen-copied into the
# build dir with one Zephyr fixup (Types.h size_t clashes with picolibc).
#
# Faux86 is GPL-2.0-or-later; this binary is GPL-2.0 by linking it.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza-faux86 LANGUAGES C CXX ASM)

# ── Faux86-remake tree (untouched) ──────────────────────────────────
if(NOT DEFINED FAUX86)
  set(FAUX86 "$ENV{HOME}/github/Faux86-remake")
endif()
if(NOT EXISTS "${FAUX86}/src/VM.cpp")
  message(FATAL_ERROR
    "Faux86-remake sources not found at ${FAUX86}. Clone "
    "https://github.com/ArnoldUK/Faux86-remake or pass -DFAUX86=<path>.")
endif()

# Gen-copy the core into the build dir with the one Zephyr fixup: Types.h
# typedefs size_t on the non-win32/non-arduino path, clashing with picolibc.
# Replace that typedef with <stddef.h>. Checkout stays pristine.
set(FAUX86_GEN ${CMAKE_BINARY_DIR}/faux86-gen)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${FAUX86}/src ${FAUX86_GEN})
execute_process(COMMAND sed -i.bak
  "s@typedef unsigned int size_t;@#include <stddef.h>@"
  ${FAUX86_GEN}/Types.h)

file(GLOB FAUX86_CORE_SRC ${FAUX86_GEN}/*.cpp)
list(REMOVE_ITEM FAUX86_CORE_SRC
  ${FAUX86_GEN}/netcard.cpp   # NE2000 -- needs libpcap, off by default
  ${FAUX86_GEN}/packet.cpp
)

# ── app-local glue ──────────────────────────────────────────────────
set(FAUX86_APP_SRC
  src/faux86_host.cpp
  src/faux86_main.cpp
  src/faux86_clock.c
  src/faux86_pack.S
)
if(CONFIG_FAUX86_PRESENT_SEMIHOST)
  list(APPEND FAUX86_APP_SRC src/faux86_present_semihost.c)
elseif(CONFIG_FAUX86_PRESENT_DISPLAY)
  list(APPEND FAUX86_APP_SRC src/faux86_present_display.c)
else()
  list(APPEND FAUX86_APP_SRC src/faux86_present_none.c)
endif()

target_sources(app PRIVATE ${FAUX86_CORE_SRC} ${FAUX86_APP_SRC})

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include   # <circle/*> shims (new/alloc/util/sched)
  ${FAUX86_GEN}                          # patched Types.h + core headers
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# DEPTH=16 is the render path Faux86-remake actually implements (win32/Pi
# ship it; DEPTH=32's vga_framebuffer fill is unimplemented -> black frames).
# The host blit converts RGB565 -> XRGB for the present seam.
target_compile_definitions(app PRIVATE DEPTH=16)

# The core is upstream; silence its warnings (as the DOSBox-X port does).
set_source_files_properties(${FAUX86_CORE_SRC} PROPERTIES COMPILE_OPTIONS "-w")

# ── bake ROMs + boot floppy into rodata (.incbin) ───────────────────
# All four come straight from the (required) Faux86-remake checkout's
# data/ -- nothing copyrighted is duplicated into this repo. Override the
# boot floppy with -DFAUX86_FLOPPY=<path> (e.g. a Treasure Trap disk).
if(NOT DEFINED FAUX86_FLOPPY)
  set(FAUX86_FLOPPY ${FAUX86}/data/dosboot.img)
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${FAUX86}/data/pcxtbios.bin ${CMAKE_BINARY_DIR}/faux86_bios.bin)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${FAUX86}/data/videorom.bin ${CMAKE_BINARY_DIR}/faux86_videorom.bin)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${FAUX86}/data/asciivga.dat ${CMAKE_BINARY_DIR}/faux86_ascii.dat)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${FAUX86_FLOPPY}            ${CMAKE_BINARY_DIR}/faux86_floppy.img)
set_source_files_properties(src/faux86_pack.S PROPERTIES
  COMPILE_OPTIONS "-I${CMAKE_BINARY_DIR};-Wa,-I${CMAKE_BINARY_DIR}"
)
