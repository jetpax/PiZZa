# Provenance — vendored suspend sources

These files are vendored from the third-party experiment repo
**`bhoot1234567890/vc4-suspend`** (commit `24159e2`), which itself
wraps librerpi/rpi-open-firmware to add VPU suspend-to-RAM.

That repo was never run on hardware and is not actively maintained.
Free PiZZa took its suspend sources as a starting point and has been
fixing them against real silicon (Pi Zero 2 W). Rather than carry a
growing patch stack against an abandoned upstream, we vendor these
files directly and edit them in place.

## Files

| File | Origin | Free PiZZa changes |
|---|---|---|
| `suspend.c` | vc4-suspend | GPIO17 wake retarget; fixed swapped APHY register addresses (`PHY_BIST_CNTRL_SPR` 0x7EE06080, `ADDR_DLL_LOCK_STAT` 0x7EE06020); relaxed post-RESTRT SDUP wait |
| `boot_suspend_hook.c` | vc4-suspend | GPIO17 wake message |
| `boot_suspend_hook.h` | vc4-suspend | unchanged |
| `resume.S` | vc4-suspend | unchanged (known-suspect; see HANDOVER §3.2) |
| `state.h` | vc4-suspend | unchanged |
| `uart_wrappers.c` | vc4-suspend | unchanged |
| `tests/` | vc4-suspend | unchanged (host-side, not built by the Docker firmware build) |

The corresponding changes to librerpi/rpi-open-firmware itself
(`romstage.c`, `firmware/Makefile`, `trap.c`) remain as patches under
`../integration/`, since that firmware is a live upstream we intend to
track and potentially contribute back to.

Original vc4-suspend license/terms apply to the vendored portions; see
the upstream repo.
