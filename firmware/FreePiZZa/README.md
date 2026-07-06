# Free PiZZa

Open VPU boot firmware for the Pi Zero 2 W: replaces the closed
Broadcom `bootcode.bin`/`start.elf` chain with our own `bootcode.bin`
that boots PiZZa (Zephyr, AArch64) unchanged and adds
**suspend-to-RAM** (~2–5 mA target, sub-second GPIO3 wake) — the
middle ground stock firmware never shipped.

Work order: `~/github/SS/notes/Free PiZZa WO.md` (rev B). Read it
first — it carries the phase plan (G0a→G0b→G1→G2→G3 PoC → recovery
ladder → HDMI), the honest lost-vs-kept ledger, and the risk table.

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
| vc4-suspend | `~/github/vc4-suspend` | suspend/wake sources + Dockerfile (never HW-run — we own G0b first-light) |
| rpi-open-firmware | `~/github/vc4-suspend/upstream` (submodule) | base VPU bootcode |
| lk-overlay | `~/github/lk-overlay` | mailbox server, USB-PHY recipe, HVS/PV/HDMI (unfinished) |
| Linux (RPi downstream) | `~/github/linux` | register-level spec: vc4 DRM, clk-bcm2835, bcm2835-power |

## Phase 0 layout

```
firmware/FreePiZZa/
├── README.md               <- this file
├── setup-toolchain.sh      <- 0a: Linux port of vc4-suspend's toolchain script (mines Dockerfile)
├── build.sh                <- 0b/0c: reproducible build orchestrator (pristine or +suspend)
├── sd-card/                <- companion boot files (config.txt, cmdline.txt)
└── integration/            <- 0c: OUR integration patch (romstage hooks + Makefile OBJ)
    ├── 0001-romstage-suspend-hooks.patch
    └── 0002-firmware-makefile-suspend-obj.patch
```

The patches live under version control here (not as uncommitted
submodule edits — which is where vc4-suspend lost theirs).

## G0a — pristine boot (first light)

Build the unmodified upstream `bootcode.bin`, flash to a bench Zero 2 W
with UART wired to GPIO14/15 @ 115200 8N1, expect `SDRAM initialization
completed successfully!` + a VPU monitor prompt. Validates our
toolchain, the flash path, SDRAM init on our silicon, and UART.

## G0b — integrated suspend (first-ever)

Apply the two patches, rebuild (fast — Docker toolchain layer cached),
first attempt at `enter_suspend_mode()` → SDRAM self-refresh → VPU
`sleep` → GPIO3-edge wake. Measure quiescent current with USB inline
meter or PPK2. Partial results are data, not failure.
