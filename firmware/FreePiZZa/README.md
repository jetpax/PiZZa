# Free PiZZa

Open VPU boot firmware for the Pi Zero 2 W: replaces the closed
Broadcom `bootcode.bin`/`start.elf` chain with our own `bootcode.bin`,
adding **suspend-to-RAM** (~2–5 mA target, sub-second GPIO17 wake) —
the middle ground stock firmware never shipped.

Work order: `~/github/SS/notes/Free PiZZa WO.md` (rev B). It carries
the phase plan, the honest lost-vs-kept ledger, and the risk table.
Session state and the live bug list live in `HANDOVER.md` (gitignored).

## Why this is not an app

Everything under `../../apps/` is a Zephyr application built with the
Zephyr SDK. Free PiZZa is **VPU-side scalar firmware** built with
`vc4-elf-gcc` (itszor/vc4-toolchain — unrelated to `vc4asm`, the QPU
shader assembler used by apps/wipeout). Its artifact is a
`bootcode.bin` that is card-global: it replaces the boot chain for
the whole card, so Free PiZZa cards are dedicated single-image
cards.

## Layout

```
firmware/FreePiZZa/
├── README.md            <- this file
├── Dockerfile           <- self-contained build (clones rpi-open-firmware, links bootcode.bin)
├── Makefile             <- `make docker-build`
├── .dockerignore
├── flash-freepizza.sh   <- copy build/bootcode.bin to the SD card
├── src/                 <- VENDORED suspend sources (ours; fixes baked in — see src/NOTICE.md)
│   ├── suspend.c            SDRAM self-refresh, ARM power-gate, VPU sleep, wake/resume
│   ├── boot_suspend_hook.c  enter_suspend_mode / check_resume_from_suspend
│   ├── resume.S             VPU register save + resume entry
│   ├── state.h              suspend_state_t layout
│   ├── uart_wrappers.c
│   └── tests/               host-side unit tests (not built by the firmware image)
├── integration/         <- patches against librerpi/rpi-open-firmware ONLY
│   ├── 0001-romstage-suspend-hooks.patch      hook enter_suspend_mode() into _main
│   ├── 0002-firmware-makefile-suspend-obj.patch  link SUSPEND_OBJ into bootcode.elf
│   ├── 0004-diag-pl011-breadcrumbs.patch      DIAGNOSTIC ONLY (superset of 0001)
│   └── 0005-trap-gpio0-wake-handler.patch     sleh_irq handles GPIO wake IRQ
└── sd-card/             <- companion boot files (config.txt, cmdline.txt)
```

### Patches vs vendored source

We touch two upstreams. **librerpi/rpi-open-firmware** is a live
project we track and may contribute back to, so our delta against it
stays as small reviewable patches in `integration/` (pinned to commit
`2725d94`). The **suspend sources** came from the abandoned,
never-HW-run `bhoot1234567890/vc4-suspend`; they are effectively ours
now, so we vendor them under `src/` and edit in place rather than
carry a growing patch stack against a dead upstream. See
`src/NOTICE.md` for provenance and the specific fixes.

## Build

```sh
cd firmware/FreePiZZa
make docker-build      # -> build/bootcode.bin
```

Self-contained: the Dockerfile builds the vc4 toolchain (~30 min cold,
cached after), clones rpi-open-firmware at the pinned SHA, applies the
`integration/` patches, compiles `src/`, and links `bootcode.bin`. No
external checkout required. `build/` is gitignored.

## Flash

```sh
make flash             # runs flash-freepizza.sh -> /Volumes/FREEPIZZA
```

UART on GPIO14/15 @ 115200 8N1. Wake button on GPIO17 (pin 11) to GND.

## Status (Phase 0, 2026-07-06)

First light achieved on a bench Zero 2 W:

- **Boot + suspend + wake work.** Full driver log, `enter_suspend_mode()`
  runs, SDRAM enters self-refresh, PLL powers down, GPIO17 press wakes
  the VPU, and the resume path re-locks the DDR PLL, re-inits SDRAM
  (mem test passes — contents survived self-refresh), and re-ungates ARM.
- **Two known blockers** to a clean round-trip: a ~4-bit checksum
  mismatch in the saved state, and a `pc=0x2` fault when `suspend_enter`
  returns. See `HANDOVER.md` §3.
- **Suspend current: 76 mA** (no HAT), well above the 2–5 mA target —
  ARM is only reset-held, not domain-off. Separate line of work.

Note: pre-`pl011_uart_init` boot bytes come out at the mask-ROM baud,
so a 115200 terminal shows a few garbled characters before the clean
log begins. That is expected, not a fault.
