# SPDX-License-Identifier: Apache-2.0
#
# apps/lib/btinput -- helper for consuming Zephyr apps.
#
# A minimal Classic BT (BR/EDR) HID host + boot keyboard parser, factored out
# of apps/BtK1 so any PiZZa app can link BT input the same way it links
# apps/lib/sdl2shim. The consumer drives events into its own seam (SDL /
# termdirect) via btinput_set_key_cb().
#
# Usage from an app CMakeLists.txt:
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/btinput/btinput.cmake)
#   target_sources(app PRIVATE ${BTINPUT_SOURCES})
#   target_include_directories(app PRIVATE ${BTINPUT_INCLUDE_DIRS})
# plus, in prj.conf:
#   CONFIG_BTINPUT=y
#   CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB="firmware/<patchram>.hcd"

set(BTINPUT_DIR ${CMAKE_CURRENT_LIST_DIR})

set(BTINPUT_SOURCES
  ${BTINPUT_DIR}/src/hid_host_br.c
  ${BTINPUT_DIR}/src/hid_host_le.c
  ${BTINPUT_DIR}/src/kbd_report.c
  ${BTINPUT_DIR}/src/mouse_report.c
)

# Opt-in pieces (Kconfig-gated so plain consumers like the BtK1 harness
# compile nothing extra). The SDL seam additionally needs the consumer to
# link apps/lib/sdl2shim (it submits into that event queue).
if(CONFIG_BTINPUT_SEAM_SDL)
  list(APPEND BTINPUT_SOURCES ${BTINPUT_DIR}/src/seam_sdl.c)
endif()
if(CONFIG_BTINPUT_MANAGER)
  list(APPEND BTINPUT_SOURCES ${BTINPUT_DIR}/src/manager.c)
endif()

set(BTINPUT_INCLUDE_DIRS
  ${BTINPUT_DIR}
  ${BTINPUT_DIR}/src
)
