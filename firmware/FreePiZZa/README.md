# Free PiZZa

Open VPU boot firmware for the Pi Zero 2 W: replaces the closed
Broadcom `bootcode.bin`/`start.elf` chain with our own `bootcode.bin`
that boots PiZZa (Zephyr, AArch64) unchanged and adds
**suspend-to-RAM** (~2–5 mA, sub-second GPIO3 wake) — the middle
ground stock firmware never shipped.

Work order: `~/github/SS/notes/Free PiZZa WO.md` (rev B). Read it
first — it carries the phase plan (G0 reproduce → chainload → USB PHY
→ suspend PoC → recovery ladder → HDMI), the honest lost-vs-kept
ledger, and the risk table.

## Why this is not an app

Everything under `../../apps/` is a Zephyr application built with the
Zephyr SDK. Free PiZZa is **VPU-side scalar firmware** built with
`vc4-elf-gcc` (itszor/vc4-toolchain — unrelated to `vc4asm`, the QPU
shader assembler used by apps/wipeout). Its artifact is a
`bootcode.bin` that is card-global: it replaces the boot chain for
every OS on the card, so Free PiZZa cards are dedicated single-image
cards, not PINN menu entries.

## Prior art (local checkouts)

| Tree | Path | Role |
| --- | --- | --- |
| vc4-suspend | `~/github/vc4-suspend` | suspend/wake sequence + toolchain script (Phase 0 base) |
| rpi-open-firmware | `~/github/rpi-open-firmware` | minimal bootcode underneath vc4-suspend |
| lk-overlay | `~/github/lk-overlay` | mailbox server, USB-PHY recipe, HVS/PV/HDMI (unfinished) |
| Linux (RPi downstream) | `~/github/linux` | register-level spec: vc4 DRM, clk-bcm2835, bcm2835-power |

## Layout (grows with the phases)

- `setup-toolchain.sh` — Phase 0 deliverable: Linux port of
  vc4-suspend's toolchain bootstrap (builds `vc4-elf-gcc`, pinned
  commits, cached).

Nothing else yet — Phase 0 has not started.
