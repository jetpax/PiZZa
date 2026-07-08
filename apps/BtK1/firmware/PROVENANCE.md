# BT patchram blobs for the Pi Zero 2W wireless module

All fetched 2026-07-08 from RPi-Distro/bluez-firmware, branch `bookworm`
(the exact files current RPiOS installs under /lib/firmware/).
License: redistributable binary (LICENSE.cypress / LICENSE.synaptics in the
repo). Redistribution permitted as part of a product with the radio.

## The mapping (from debian/bluez-firmware.links)

RPiOS never loads a generic blob on a Zero 2W — the kernel (btbcm) reads the
ROM local-name + board compatible and requests
`BCM43430<rev>.raspberrypi,model-zero-2-w.hcd`, which the package symlinks to
the **Synaptics** firmware:

```
synaptics/SYN43430A1.hcd <- BCM43430A1.raspberrypi,model-zero-2-w.hcd
synaptics/SYN43430B0.hcd <- BCM43430B0.raspberrypi,model-zero-2-w.hcd
```

Two module revisions shipped on Zero 2W boards:
- **43436S**  -> BT ROM reports `BCM43430A1` (lmp_subver 0x2209) -> SYN43430A1.hcd
- **43436**   -> BT ROM reports `BCM43430B0` (lmp_subver 0x410c) -> SYN43430B0.hcd

**This unit is the 43436S variant**: our Zephyr brcmfmac Wi-Fi (working) runs
`brcmfmac43436s-sdio.bin`, whose firmware self-identifies as
"BCM43430A1 7.45.96.s1" (see zephyr drivers/wifi/brcmfmac/brcmfmac_core.c).
=> the BT patchram for this unit is **SYN43430A1.hcd**.

## Files

- `SYN43430A1.hcd` — 46979 B,
  sha256 55071227c94d86369d04f9aff3bbfd4197a78a53dc350295123e1a8b048bba8f
  https://raw.githubusercontent.com/RPi-Distro/bluez-firmware/bookworm/debian/firmware/synaptics/SYN43430A1.hcd
  **ACTIVE** for this unit. NOTE: differs substantially from the generic
  broadcom/BCM43430A1.hcd (30049 B) — do not substitute.
- `SYN43430B0.hcd` — 44376 B,
  sha256 338c2c6631131f516bfc7e64ef0872bd0402e1f98ef9d0c900eef0c814d90a25
  https://raw.githubusercontent.com/RPi-Distro/bluez-firmware/bookworm/debian/firmware/synaptics/SYN43430B0.hcd
  Staged for 43436 (non-S) units. Byte-identical to broadcom/BCM43430B0.hcd.
- `BCM43430B0.hcd` — 44376 B, same sha as SYN43430B0.hcd (identical file).
  First attempt 2026-07-08: **wrong for this unit** (B0 firmware on A1
  silicon) — Write_RAM/Launch_RAM all ack status 0, then the launched fw is
  dead on boot (no response to the post-launch HCI_RESET). Kept as a
  cautionary artifact; superseded by SYN43430A1.hcd.

## Empirical confirmation hook

The bring-up app logs the controller local-name + lmp_subversion after
bt_enable(); expect "BCM43430A1..." / 0x2209 on this unit. If a future unit
reports 0x410c, switch CONFIG_AIROC_CUSTOM_FIRMWARE_HCD_BLOB to
SYN43430B0.hcd.
