# SPDX-License-Identifier: AGPL-3.0-or-later
#
# PiZZa -- WipEout demo. Stand-alone Zephyr application that links the
# phoboslab/wipeout-rewrite engine (software renderer) against the
# rpi_zero_2w board and presents 320x240 frames to the BCM2835
# VideoCore HDMI framebuffer via HVS hardware scaling.
#
# The engine source lives outside the PiZZa repo at
# ~/github/wipeout-rewrite. The port preserves the upstream tree --
# zero edits -- by:
#
#   1. Replacing src/render_software.c with our SMP-patched copy
#      (the upstream file is excluded from COMMON_SRC below).
#   2. Replacing src/platform_*.c with src/platform_zephyr.c.
#   3. --wrap=file_exists / file_load / file_store on the engine's
#      libc-backed utils.c so SD-card I/O lands on Zephyr's fs_*
#      with a /SD: prefix instead of POSIX fopen() (which is not
#      wired to FATFS in this image).
#
# The renderer is AGPL-3.0-or-later (matching wipeout-rewrite). The
# rest of PiZZa is Apache-2.0; this binary is AGPL by virtue of
# linking the engine, same model as the Jet demo.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza-wipeout C)

# Pull in the sample usbd helper (sample_usbd_init_device + sample_usbd.h)
# the same way PizzaShell / Jet do. Only needed when the USB device
# stack is in the build; rpi_zero_2w turns it on.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

if(NOT DEFINED WIPEOUT_SRC_DIR)
  set(WIPEOUT_SRC_DIR "$ENV{HOME}/github/wipeout-rewrite/src")
endif()
if(NOT EXISTS "${WIPEOUT_SRC_DIR}/render_software.c")
  message(FATAL_ERROR
    "wipeout-rewrite sources not found at ${WIPEOUT_SRC_DIR}. Clone "
    "https://github.com/phoboslab/wipeout-rewrite and either keep it "
    "at ~/github/wipeout-rewrite or pass -DWIPEOUT_SRC_DIR=<path>.")
endif()

# Upstream engine sources (everything in src/ except the host platform
# backends and the original render_software.c -- we ship our own).
set(WIPEOUT_COMMON_SRC
  ${WIPEOUT_SRC_DIR}/utils.c
  ${WIPEOUT_SRC_DIR}/types.c
  ${WIPEOUT_SRC_DIR}/system.c
  ${WIPEOUT_SRC_DIR}/mem.c
  ${WIPEOUT_SRC_DIR}/input.c
  ${WIPEOUT_SRC_DIR}/wipeout/race.c
  ${WIPEOUT_SRC_DIR}/wipeout/camera.c
  ${WIPEOUT_SRC_DIR}/wipeout/object.c
  ${WIPEOUT_SRC_DIR}/wipeout/droid.c
  ${WIPEOUT_SRC_DIR}/wipeout/ui.c
  ${WIPEOUT_SRC_DIR}/wipeout/hud.c
  ${WIPEOUT_SRC_DIR}/wipeout/image.c
  ${WIPEOUT_SRC_DIR}/wipeout/game.c
  ${WIPEOUT_SRC_DIR}/wipeout/menu.c
  ${WIPEOUT_SRC_DIR}/wipeout/main_menu.c
  ${WIPEOUT_SRC_DIR}/wipeout/ingame_menus.c
  ${WIPEOUT_SRC_DIR}/wipeout/title.c
  ${WIPEOUT_SRC_DIR}/wipeout/intro.c
  ${WIPEOUT_SRC_DIR}/wipeout/scene.c
  ${WIPEOUT_SRC_DIR}/wipeout/ship.c
  ${WIPEOUT_SRC_DIR}/wipeout/ship_ai.c
  ${WIPEOUT_SRC_DIR}/wipeout/ship_player.c
  ${WIPEOUT_SRC_DIR}/wipeout/track.c
  ${WIPEOUT_SRC_DIR}/wipeout/weapon.c
  ${WIPEOUT_SRC_DIR}/wipeout/particle.c
  ${WIPEOUT_SRC_DIR}/wipeout/sfx.c
)

# Port sources. The rasteriser file is picked by Kconfig:
#   CONFIG_WIPEOUT_RENDERER_SMP (default) -> render_software_smp.c,
#     the upstream renderer with the band edit applied + gfx_parallel_run
#     wiring.
#   CONFIG_WIPEOUT_RENDERER_PIZZA -> render_software_pizza.c, the
#     Q16/RGB565 rewrite (scaffolding only; see
#     notes/wipeout_pizza_renderer_spec.md).
# gfx_parallel_zephyr.c is the persistent worker pool (band 0 on
# caller); platform_zephyr.c is the platform.h backend + the Zephyr
# main thread that drives the game loop; wipeout_glue.c wraps the
# engine's file_* helpers onto Zephyr fs_* + /SD: prefix.
if(CONFIG_WIPEOUT_RENDERER_PIZZA)
  set(WIPEOUT_RENDERER_SRC src/render_software_pizza.c)
else()
  set(WIPEOUT_RENDERER_SRC src/render_software_smp.c)
endif()

set(WIPEOUT_PORT_SRC
  ${WIPEOUT_RENDERER_SRC}
  src/gfx_parallel_zephyr.c
  src/platform_zephyr.c
  src/wipeout_glue.c
)

target_sources(app PRIVATE
  ${WIPEOUT_COMMON_SRC}
  ${WIPEOUT_PORT_SRC}
)

# Engine headers (#include "platform.h", "render.h", etc. without
# subdirs) and its bundled stb / pl_mpeg / qoa.
target_include_directories(app PRIVATE
  ${WIPEOUT_SRC_DIR}
  ${WIPEOUT_SRC_DIR}/libs
  src
)

target_compile_definitions(app PRIVATE
  RENDERER_SOFTWARE=1
)

# Loosen the warnings the engine trips with -Werror'ish defaults --
# upstream builds with -Wno-unused-variable and uses 1.0/255.0 idioms
# the compiler can't always prove safe.
target_compile_options(app PRIVATE
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-missing-braces
  -Wno-incompatible-pointer-types
)

# Redirect the engine's libc-backed file helpers to our Zephyr fs_*
# implementations. The originals stay in utils.c (unused at link time
# after wrapping) so no upstream edit is needed.
# PUBLIC, not PRIVATE: app is a Zephyr-built static lib, so PRIVATE
# link options never reach the final ELF link. PUBLIC propagates them
# through the target's INTERFACE_LINK_OPTIONS to the parent.
target_link_options(app PUBLIC
  -Wl,--wrap=file_exists
  -Wl,--wrap=file_load
  -Wl,--wrap=file_store
)
