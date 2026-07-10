<!-- SPDX-License-Identifier: Apache-2.0 -->
# btinput

Bluetooth HID input for PiZZa apps: a minimal Classic BT (BR/EDR) HID
host with an optional BLE HOGP backend, a connection manager, and an
SDL event seam that feeds `s2s_event_submit()` for games built on
[`apps/lib/sdl2shim`](../sdl2shim). Handles keyboards, mice, and
combos concurrently. Proven on `rpi_zero_2w`.

## Consuming it

The easy path, for games on the sdl2 shim:

```c
btinput_seam_sdl_set_keymap(pad_map, ARRAY_SIZE(pad_map)); /* optional */
btinput_seam_sdl_attach();   /* events -> sdl2shim queue      */
btinput_manager_start();     /* bring-up + policy, own thread */
```

... with `CONFIG_BTINPUT_MANAGER=y` + `CONFIG_BTINPUT_SEAM_SDL=y` (and
`CONFIG_BTINPUT_HOGP=y` for BLE mice) in the consuming app's board
conf, and the patchram blob path in
`CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB`. See
[`apps/minivmac/boards/rpi_zero_2w.conf`](../../minivmac/boards/rpi_zero_2w.conf)
and [`apps/DOSBox`](../../DOSBox) for reference consumers.

See [`NOTES.md`](NOTES.md) for the raw API, connection policy details,
Kconfig walk-through, layout, and parser tests.
