# apps/lib/btinput — Bluetooth HID input for PiZZa apps

A minimal **Classic BT (BR/EDR) HID host**. Zephyr ships the BR/EDR ACL + SSP +
L2CAP machinery but no HID host profile, so this provides it: the two HID
L2CAP channels (PSM 0x11 control, 0x13 interrupt), boot keyboard report
diffing, and device-initiated reconnection. It was proven on hardware by a
dedicated bring-up harness and links into a consumer exactly like
`apps/lib/sdl2shim`.

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
that pair host-ward — or let the manager's boot inquiry handle it.

`CONFIG_BTINPUT=y` selects the core Classic-BT host pieces. Board/policy
tunables — the aarch64 BT stack sizes, `BT_MAX_CONN`/`BT_MAX_PAIRED`, legacy
pairing (`BT_SMP_SC_PAIR_ONLY=n`), and the settings backend — stay in the
consuming app's board conf (see `apps/minivmac/boards/rpi_zero_2w.conf` for
the reference set).

## The easy path: manager + SDL seam (M4.4)

Games skip all of the above. With `CONFIG_BTINPUT_MANAGER=y` +
`CONFIG_BTINPUT_SEAM_SDL=y` (and `CONFIG_BTINPUT_HOGP=y` for BLE mice) the
whole thing is:

```c
btinput_seam_sdl_set_keymap(pad_map, ARRAY_SIZE(pad_map)); /* optional */
btinput_seam_sdl_attach();   /* events -> sdl2shim queue      */
btinput_manager_start();     /* bring-up + policy, own thread */
```

The **manager** (`src/manager.c`) runs the hardware-proven connection policy: FAT-backed
bond storage, `bt_enable` + patchram off the consumer's critical path, boot
BR inquiry when nothing is bonded (pages the first discoverable HID-class
device -- SSP Just Works), per-device rescue pages for bonded Classic devices
(their device-initiated reconnects are flaky, HW-proven cadences), and an LE
scan that connects bonded/HID-flavored advertisers, attaches them via
`btinput_le_attach()`, and blocklists LE faces that never arm input (the
8BitDo's vendor-only BLE side).

The **SDL seam** (`src/seam_sdl.c`) registers the sinks and submits
`SDL_KEYDOWN/UP` (HID usages ARE SDL scancodes; optional per-pad remap
table), `SDL_MOUSEMOTION` (absolute + relative), `SDL_MOUSEBUTTON*` and
`SDL_MOUSEWHEEL` through `s2s_event_submit()` -- games read them with zero
code changes. `apps/DOSBox` is the reference consumer (M4.5).

## Layout

```
btinput.h          public API (init, pair_mode, sinks, seam, manager)
btinput.cmake      exports BTINPUT_SOURCES + BTINPUT_INCLUDE_DIRS  <- the seam
Kconfig            CONFIG_BTINPUT (+HOGP, +SEAM_SDL, +MANAGER), log module
src/hid_host_br.c  N-device Classic HID host (lifecycle, channels, demux)
src/hid_host_le.c  LE HOGP backend (CONFIG_BTINPUT_HOGP)
src/kbd_report.c   boot keyboard diff parser (pure C, host-testable)
src/mouse_report.c boot mouse parser (pure C, host-testable)
src/seam_sdl.c     events -> sdl2shim queue (CONFIG_BTINPUT_SEAM_SDL)
src/manager.c      bring-up + connection policy (CONFIG_BTINPUT_MANAGER)
firmware/          shared BT patchram blobs (gitignored; PROVENANCE.md)
tests/             host unit tests for the parsers
```

## Parser tests (host)

```
cd tests && cc -Wall -o /tmp/t  test_kbd_report.c   ../src/kbd_report.c   && /tmp/t
cd tests && cc -Wall -o /tmp/tm test_mouse_report.c ../src/mouse_report.c && /tmp/tm
```

## Roadmap (M4)

M4.4 (SDL seam) and M4.5 (demo consumer: `apps/DOSBox`) are implemented.
Remaining: the 8BitDo keymap mini-pass (four provisional entries in the
DOSBox pad map), and a Classic BT mouse to exercise the BR mouse path.
