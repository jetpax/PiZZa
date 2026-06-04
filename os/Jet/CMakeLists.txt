# SPDX-License-Identifier: AGPL-3.0-or-later
#
# PiZZa -- Jet demo. Stand-alone Zephyr application that links the Jet
# software 3D rasteriser (AGPL-3.0-or-later) against the rpi_zero_2w
# board and pushes frames to the BCM2835 VideoCore HDMI framebuffer in
# native RGB565. Lives outside PizzaShell so the Apache-2.0 distribution
# stays clean -- this binary is AGPL by virtue of linking Jet.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza-jet CXX C)

# Jet engine source tree (kept outside the PiZZa repo at
# ~/github/Jet). The user can override JET_SRC_DIR to point elsewhere.
if(NOT DEFINED JET_SRC_DIR)
  set(JET_SRC_DIR "$ENV{HOME}/github/Jet/src")
endif()
if(NOT EXISTS "${JET_SRC_DIR}/Jet.hpp")
  message(FATAL_ERROR
    "Jet sources not found at ${JET_SRC_DIR}. Clone "
    "https://github.com/CubeCoders/Jet and either symlink it as "
    "~/github/Jet or pass -DJET_SRC_DIR=<path>.")
endif()

# Pull in Jet's own CMakeLists, which builds a STATIC library target
# named "Jet" from its .cpp files. Its target_include_directories
# exposes JET_SRC_DIR as PUBLIC, so anything that links Jet
# automatically resolves "Jet.hpp" and "JetConfig.hpp".
add_subdirectory("${JET_SRC_DIR}" "${CMAKE_BINARY_DIR}/jet" EXCLUDE_FROM_ALL)

# Jet's CMakeLists does not know about Zephyr. Link it to the
# zephyr_interface INTERFACE target to inherit Zephyr's compile
# options, include search paths and compile definitions -- otherwise
# Jet's translation units miss the right -march / -mcpu and the
# Zephyr-provided libc/libstdc++ wiring.
target_link_libraries(Jet PRIVATE zephyr_interface)

# Our per-frontend JetConfig.hpp must resolve BEFORE the upstream
# Config.example.hpp on Jet's include path -- prepend our src/ dir.
target_include_directories(Jet BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Boards with the USB device stack drive the shell over USB CDC ACM
# and need the usbd_context handle from the upstream sample helper.
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

target_sources(app PRIVATE src/main.cpp)
target_link_libraries(app PRIVATE Jet)
