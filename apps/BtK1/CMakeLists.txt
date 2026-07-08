#
# WO-BT-K1 — 8BitDo Micro (K mode) BLE HOGP input on PiZZa Zero 2W.
# Bring-up vehicle: starts as an LE observer (M0/M1 transport proof) and
# grows into the HOGP keyboard client (M2-M4).
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bt_k1_bringup)

target_sources(app PRIVATE
  src/main.c
  src/hogp.c
  src/bthid_br.c
  src/kbd_report.c
)
