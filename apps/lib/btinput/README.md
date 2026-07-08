# apps/lib/btinput — Bluetooth HID input for PiZZa apps

A minimal **Classic BT (BR/EDR) HID host**. Zephyr ships the BR/EDR ACL + SSP +
L2CAP machinery but no HID host profile, so this provides it: the two HID
L2CAP channels (PSM 0x11 control, 0x13 interrupt), boot keyboard report
diffing, and device-initiated reconnection. It is the reusable capability
factored out of the `apps/BtK1` bring-up harness, and links into a consumer
exactly like `apps/lib/sdl2shim`.

Proven on `rpi_zero_2w` / CYW43436 / SYN43430A1 fw / 8BitDo Micro (K mode).
See the memory `reference_zephyr_classic_hid_host` for the hard-earned rules
(accept inbound as central, stay passive on inbound, per-face BD_ADDRs, etc.).

## Consuming it

```cmake
# CMakeLists.txt
include(${CMAKE_CURRENT_SOURCE_DIR}/../lib/btinput/btinput.cmake)
target_sources(app PRIVATE ${BTINPUT_SOURCES})
target_include_directories(app PRIVATE ${BTINPUT_INCLUDE_DIRS})
```

```
# Kconfig  (the app's own Kconfig, so CONFIG_BTINPUT is known)
rsource "../lib/btinput/Kconfig"
```

```
# prj.conf
CONFIG_BTINPUT=y
CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB="firmware/<patchram>.hcd"
```

```c
/* code */
#include "btinput.h"

static void on_key(uint8_t usage, bool pressed, void *user) { ... }

btinput_set_key_cb(on_key, NULL);
bt_enable(NULL);
/* ... settings_load() for bond persistence ... */
btinput_init();          /* servers + connectable, accept inbound as central */
```

`CONFIG_BTINPUT=y` selects the core Classic-BT host pieces. Board/policy
tunables — the aarch64 BT stack sizes, `BT_MAX_CONN`/`BT_MAX_PAIRED`, legacy
pairing (`BT_SMP_SC_PAIR_ONLY=n`), and the settings backend — stay in the
consuming app's `prj.conf` (see `apps/BtK1/prj.conf` for the reference set).

## Layout

```
btinput.h          public API (btinput_init, btinput_set_key_cb, hid_start/stop/active)
btinput.cmake      exports BTINPUT_SOURCES + BTINPUT_INCLUDE_DIRS  <- the seam
Kconfig            CONFIG_BTINPUT + log module
src/hid_host_br.c  Classic HID host (L2CAP servers, channels, reconnect)
src/kbd_report.c   boot keyboard diff parser (pure C, host-testable)
tests/             host unit tests for the parser
```

## Parser tests (host)

```
cd tests && cc -Wall -o /tmp/t test_kbd_report.c ../src/kbd_report.c && /tmp/t
```

## Roadmap (M4)

Keyboard-only, single device today. Planned: a concurrent HID **mouse**
(`mouse_report.c` + per-device state table), an SDL seam (`seam_sdl.c`) that
turns usages into `SDL_KEYDOWN`/`SDL_MOUSEMOTION` for the sdl2shim event queue,
and board-DTS promotion of the BT UART node. See `apps/BtK1/notes/M4_PLAN.md`.
