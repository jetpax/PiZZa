# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizzaboot)

# USB CDC ACM menu mirror needs the usbd_context init helper from the
# Zephyr USB samples (same pattern as PizzaShell).
if(CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

target_sources(app PRIVATE
  src/main.c
  src/hdmi.c
  ../lib/bootsel/bootsel.c
)
target_include_directories(app PRIVATE ../lib/bootsel)
