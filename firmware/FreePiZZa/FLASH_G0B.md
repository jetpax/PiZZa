# G0b — First suspend/wake attempt

**Artifact:** `build/bootcode.bin` (108,864 bytes — pristine
rpi-open-firmware + Free PiZZa suspend module linked in, **GPIO17
wake variant**).

Built by applying **all three** integration patches to the pristine
librerpi upstream + vc4-suspend `src/`, then rebuilding via Docker:

  - `0001-romstage-suspend-hooks.patch` — hook `check_resume_from_suspend()` and `enter_suspend_mode()` into `_main()`
  - `0002-firmware-makefile-suspend-obj.patch` — link `SUSPEND_OBJ` into `bootcode.elf`
  - `0003-gpio17-wake-button.patch` — retarget wake button to GPIO17 (pin 11) with pull-up + falling-edge (matches button-to-GND wiring); also fixes vc4-suspend's own code/README contradiction where the code said pull-down + rising-edge while the README diagram showed button-to-GND

The `[suspend]` diagnostic strings are visible in the linked image,
including the new `GPIO17 configured as wake source (pull-up +
falling edge)` and `To wake: press GPIO17 button (pin 11 to GND)`.

`bootcode.G0a.bin` and `bootcode.G0b.bin` (the earlier GPIO3
variant) are preserved alongside for A/B.

## What changes from G0a

- Same VPU boot up to and including `PEStartPlatform`.
- After `PEStartPlatform`, instead of launching `monitor_start()`
  (which triggers the ARM chainloader and panics on missing zImage
  as you saw), the VPU calls `enter_suspend_mode()`.
- `enter_suspend_mode()` prints its banner over UART and calls
  `suspend_enter()`, which:
  1. Saves VPU scalar registers to SDRAM at `SUSPEND_STATE_ADDR`
     (0x1FF00000, just under 512 MB).
  2. Saves PLL/GPIO/IC0/IC1/SDRAM config.
  3. Power-gates ARM (`PM_PROC` cleared with password).
  4. Puts SDRAM into JEDEC self-refresh, power-downs DDR PLL.
  5. Gates unneeded clocks, lowers VPU clock.
  6. Configures GPIO3 as wake source: pull-down + rising-edge
     detect, GPIO bank 0 interrupt routed to VPU IC0.
  7. VPU `sleep` — halts until interrupt.
- On a GPIO3 rising edge: PLL relock, SDRAM self-refresh exit + ZQ
  cal + memory pattern test, ARM power-on, register restore, return
  to `_main`, and `enter_suspend_mode()` prints
  `[suspend] Resumed successfully!`

## What you need on top of G0a

- **GPIO17 wake button.** Tactile push-button between:
  ```
  Pin 11 (GPIO17) ────┐
                      ├── tactile button ── Pin 9 (GND)
                      │
             (button idle HIGH via internal pull-up;
              press pulls LOW → falling-edge fires the wake)
  ```
  The suspend code enables **pull-up** on GPIO17 and detects the
  **falling** edge — the standard Pi-button convention. VPU IC0 GPIO
  bank-0 interrupt (intno 49) covers pins 0-27, so GPIO17 fires the
  same wake path GPIO3 would have.
- **USB power meter** inline on the Pi's USB power cable — the
  cheap ~$15 in-line meters with a mA display work; a Nordic PPK2
  is much better if you have one. What we care about:
  - Idle current before suspend (baseline)
  - Current *at* the `sleep` instruction — the number that matters
  - Current after wake (should return to baseline)

## Flash

If your card mounts as `/Volumes/FREEPIZZA` (recommended — label the
card `FREEPIZZA`), just run:

```sh
./flash-freepizza.sh
```

It copies `bootcode.bin`, `config.txt`, `cmdline.txt` to the mount,
runs `sync`, and `diskutil eject`s the card so you can pull it
without a nag dialog. Override the mount with an arg
(`./flash-freepizza.sh /Volumes/OTHER`) or pick a different bin with
`BIN=build/bootcode.G0a.bin ./flash-freepizza.sh` for A/B.

If you'd rather copy by hand:
```
bootcode.bin  <-  firmware/FreePiZZa/build/bootcode.bin (G0b GPIO17)
config.txt    <-  firmware/FreePiZZa/sd-card/config.txt (unchanged)
cmdline.txt   <-  firmware/FreePiZZa/sd-card/cmdline.txt (unchanged)
```

## Expected UART

Same VPU boot output as G0a, up through the drivers and
`Starting IPC monitor ...`. Then instead of the ARM chainloader
output, expect:

```
[suspend] System ready. Entering suspend-to-RAM...
[suspend] To wake: press GPIO17 button (pin 11 to GND)
[suspend] === Entering suspend-to-RAM ===
[suspend] GPIO17 configured as wake source (pull-up + falling edge)
[suspend] VPU IC0 GPIO bank 0 interrupt enabled
[suspend] waiting for SDRAM controller to go down...
[suspend] SDRAM clock disabled
[suspend] SDRAM in self-refresh, PLL powered down
```

Then silence — VPU is in `sleep`. **Read the current meter.**

Press the GPIO17 button (pin 11 → GND). Expected:

```
[suspend] Re-enabling DDR PLL...
[suspend] Waiting for PLL lock...
[suspend] DDR PLL locked
[suspend] Un-gating SDRAM clock...
[suspend] Resetting PHY DLLs...
[suspend] PHY DLL locked
[suspend] Restarting SDRAM controller...
[suspend] ZQ calibration complete
[suspend] SDRAM mem test passed
[suspend] SDRAM back online
[suspend] Resumed successfully!
```

Read the current again — should be back to the baseline.

## Success criteria (G0b) — three tiers

- **Minimum ("fundamentals aren't dead"):** VPU reaches `SDRAM in
  self-refresh, PLL powered down` and stops printing. Any measured
  current drop from the pre-suspend baseline counts as evidence the
  self-refresh path is doing *something*. GPIO3 wake response is a
  separate question — even if wake fails, the suspend half is data.
- **Good:** GPIO3 edge fires the resume path; UART prints through
  `Resumed successfully!`.
- **Great (Phase 3 grade):** Measured suspended current in the 2–10
  mA range. Anything under 15 mA is exciting for first light. If we
  overshoot the JOURNAL's 2–5 mA target, that's already novel.

## Failure modes we should be prepared for

| Symptom | Interpretation |
| --- | --- |
| Suspend banner prints, then hard hang (no self-refresh log) | ARM power-gate or SDRAM STBY handshake stalled — look at last successful `[suspend]` line |
| Reaches `sleep` but current unchanged | Clock gating didn't take; PLLs still spinning |
| Wakes on its own (no GPIO3 press) | Spurious IRQ — check log for wake reason line |
| `[suspend] SDRAM mem test FAILED` | Self-refresh corrupted RAM — this is the exact kind of data we want; report the pattern # |
| `[suspend] FATAL: DDR PLL failed to lock after 3 attempts` | The exit sequence's known pain point — resume.S has 3-retry logic; if all 3 fail, this is where they'd land |

Any of the above **is a valid Phase 0c outcome** — nobody has run
this code on silicon before, so partial results become the
starting point for Phase 3's hardening.

## When you have data

Paste the full UART capture (from power-on through your best wake
attempt or timeout) plus your current readings at:
- pre-suspend baseline
- while `sleep`ing
- post-wake

I'll turn that into the honest "vs the JOURNAL's 2–5 mA target"
line for the WO and the memory.
