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

Handles up to `CONFIG_BTINPUT_MAX_DEVICES` concurrent devices (keyboard +
mouse + combos). The lib registers its own connection callbacks and owns the
whole BR lifecycle -- claim, security, channel setup/accept, teardown -- so a
consumer needs no connection code at all. Input demux is by HIDP boot report
id (1 = keyboard, 2 = mouse) with length fallbacks; `SET_PROTOCOL(Boot)` is
requested on every control channel and a NAK is tolerated.

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
static void on_mbtn(uint8_t button, bool pressed, void *user) { ... }
static void on_mmove(int dx, int dy, int wheel, void *user) { ... }

btinput_set_key_cb(on_key, NULL);
btinput_set_mouse_cbs(on_mbtn, on_mmove, NULL);
bt_enable(NULL);
/* ... settings_load() for bond persistence ... */
btinput_init();  /* servers + connectable; lib drives all BR conns from here */
```

Bonded devices reconnect device-initiated with no further code. First pairing
is consumer policy: page the device's Classic address (`bt_conn_create_br` --
the lib takes the connection over), or `btinput_pair_mode(true)` for devices
that pair host-ward. See `apps/BtK1` for a discovery+page flow (mice).

`CONFIG_BTINPUT=y` selects the core Classic-BT host pieces. Board/policy
tunables — the aarch64 BT stack sizes, `BT_MAX_CONN`/`BT_MAX_PAIRED`, legacy
pairing (`BT_SMP_SC_PAIR_ONLY=n`), and the settings backend — stay in the
consuming app's `prj.conf` (see `apps/BtK1/prj.conf` for the reference set).

## Layout

```
btinput.h          public API (init, pair_mode, key + mouse sinks)
btinput.cmake      exports BTINPUT_SOURCES + BTINPUT_INCLUDE_DIRS  <- the seam
Kconfig            CONFIG_BTINPUT, BTINPUT_MAX_DEVICES, log module
src/hid_host_br.c  N-device Classic HID host (lifecycle, channels, demux)
src/kbd_report.c   boot keyboard diff parser (pure C, host-testable)
src/mouse_report.c boot mouse parser (pure C, host-testable)
tests/             host unit tests for the parsers
```

## Parser tests (host)

```
cd tests && cc -Wall -o /tmp/t  test_kbd_report.c   ../src/kbd_report.c   && /tmp/t
cd tests && cc -Wall -o /tmp/tm test_mouse_report.c ../src/mouse_report.c && /tmp/tm
```

## Roadmap (M4)

Remaining: the SDL seam (`seam_sdl.c`) turning usages into `SDL_KEYDOWN` /
`SDL_MOUSEMOTION`/`BUTTON`/`WHEEL` for the sdl2shim event queue (M4.4), then a
zero-game-code demo consumer (M4.5). See `apps/BtK1/notes/M4_PLAN.md`.
