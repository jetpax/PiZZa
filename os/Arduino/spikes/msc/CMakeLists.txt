# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pizza_msc_spike)

# Pulls in samples/subsys/usb/common/sample_usbd_init.c + headers.
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
target_sources(app PRIVATE src/main.c)
