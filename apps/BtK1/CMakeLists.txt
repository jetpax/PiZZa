#
# WO-BT-K1 — 8BitDo Micro BT keyboard input on PiZZa Zero 2W.
# Bring-up vehicle / test harness for the Classic BT HID host; the reusable
# capability (bthid_br + kbd_report) is destined for apps/lib once M4 lands.
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(btk1)

# The BT patchram (.hcd) is a gitignored vendor blob (firmware/PROVENANCE.md).
# Fail the build LOUDLY if it is missing rather than silently emitting an
# empty-patchram image -- at runtime that sends Download_Minidriver with
# nothing to write/launch and wedges the controller. This replaces the need
# for a defensive guard in the upstream Infineon driver.
if(NOT CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB)
  message(FATAL_ERROR
    "BtK1: CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB is unset -- point it at a BT patchram .hcd")
endif()
set(_btk1_blob "${CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB}")
if(NOT IS_ABSOLUTE "${_btk1_blob}")
  set(_btk1_blob "${CMAKE_CURRENT_SOURCE_DIR}/${_btk1_blob}")
endif()
if(NOT EXISTS "${_btk1_blob}")
  message(FATAL_ERROR
    "BtK1: BT patchram not found:\n  ${_btk1_blob}\n"
    "It is gitignored (vendor blob) -- fetch it per firmware/PROVENANCE.md before building.")
endif()

target_sources(app PRIVATE
  src/main.c
  src/hogp.c
  src/bthid_br.c
  src/kbd_report.c
)
