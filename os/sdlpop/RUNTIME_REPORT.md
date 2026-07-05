<!-- SPDX-License-Identifier: Apache-2.0 -->
# SDLPoP → PiZZa · Runtime report (rev A1)

Port of NagyD/SDLPoP to PiZZa (Zephyr, BCM2710A1 / 4×A53) via a minimal
SDL2 shim on the Jet/wipeout framebuffer present path. This report
separates what is **verified in simulation** from what **needs hardware
sign-off**, per the work-order guardrail: no on-hardware success is
claimed for anything only run in the emulator.

## Summary

Phases 1–5 (build, video, game render, timing+scripted input, assets/FS)
are **sim-verified** on `qemu_cortex_a53`. **Phase 6 is confirmed on real
hardware** (rpi_zero_2w, 2026-07-04): the game boots on the A53, renders on
the HDMI panel through the HVS-scaled framebuffer with correct colors, and
is playable — the intro advances and the Prince moves under `pop` shell
input over USB CDC ACM. Audio and physical controls are deferred (§5) and
stubbed behind live seams.

---

## ✅ VERIFIED IN SIMULATION (qemu_cortex_a53, TCG)

Frames below are byte-for-byte captures pulled off the guest via
semihosting (`pop_present_semihost.c` → PPM), saved under `notes/frames/`.

| # | Phase | Result | Evidence |
|---|-------|--------|----------|
| 1 | **Build integration** | SDLPoP (untouched tree) + fully-stubbed then real shim link clean for both `rpi_zero_2w` and `qemu_cortex_a53`. **No libstdc++** — link map shows only `libapp/libc/libgcc/libkernel/libzephyr`. | clean link, `grep __cxa_ = 0` |
| 2 | **Video / present** | Color bars + gray ramp + 16-step strip driven through the exact `CreateWindow→CreateRenderer→CreateTexture→UpdateTexture→RenderCopy→RenderPresent` path SDLPoP uses; pixels match expected values. | `notes/frames/01_selftest_colorbars.png` |
| 3 | **Game render** | SDLPoP boots to its intro text screen and the Prince of Persia palace title screen — full palette, fonts, surface composition, colorkeyed sprite blits. | intro + title captured (session) |
| 4 | **Timing + scripted input** | Ticks/delay on `k_uptime`/`k_timer`; a synthetic input producer (`shim_scripted.c`) feeds the `shim_event.c` queue; the scripted sequence **moves the Prince** — he turns, runs right across rooms, on the live "60 MINUTES LEFT" HUD. Motion measured: changed-pixel bbox x[76..287], torches animate every frame. | `notes/frames/02_level1_start.png`, `03_level1_moved_right.png`, `04_phase4_movement_montage.png` |
| 5 | **Assets / FS** | §4 preferred pipeline executed: `pack_assets.py` pre-converts all 929 PNGs → raw PIMG blobs + the rest of `data/` (music excluded) into one 1.3 MB pack linked into `.rodata`; `pop_assets.c` serves it as a read-only POSIX fd space. Game loads every sprite/palette through it — no PNG decoder, no FS driver on the critical path. Boot-to-gameplay from the clean image. | 1033 pack entries logged at boot |

**Build/link facts**
- Hardware `zephyr.bin`: ~1.9 MB (includes the embedded asset pack).
- Shim SDL2 surface implements exactly the §2 bucket-B manifest; A is
  header-only, C/D stub, E (SDL1.2) confirmed dead under the SDL2 path.

**Sim caveat (documented, not a defect):** qemu's TCG runs this
software-rendered workload at roughly 1/80 wall-clock, and its `vm`
clock dilates to match, so reaching gameplay through the full intro is
minutes of wall time. The sim build therefore boots straight into
level 1 (`megahit`+level arg, `CONFIG_SDLPOP_SIM_TUNING`, fades off via a
sim-only packed ini) purely to make the phase-4 capture practical. None
of that tuning is in the hardware build — it runs the stock game.

---

## ✅ VERIFIED ON HARDWARE (rpi_zero_2w, real A53 + HDMI, 2026-07-04)

Flashed and run on the board:

1. **Boots on the A53.** Banner + `[sdlpop] asset pack: 1033 entries` on
   the mini-UART (GPIO 14/15, 115200); `bcm2835_fb: HVS scaling 320x200
   virt -> 1824x984 phys` confirms the present path. USB CDC ACM
   enumerates (`usbd_core: Actual device speed 2`) — the shim brings USBD
   up at APPLICATION init (`shim_usb.c`, `sample_usbd_init_device` +
   `usbd_enable`), required because CDC auto-init is deferred on this
   silicon.
2. **Panel output via HVS-scaled plane** — `pop_present_display.c` pushes
   the 320×200 frame into `vc_fb`; the VideoCore HVS scales to the
   monitor's native mode. Intro screen and gameplay render correctly.
3. **Colors correct** — the game `[R,G,B]` → ARGB8888 conversion is right
   on real VC scanout (no R/B swap). This was the one thing sim couldn't
   validate; confirmed via the color-bar selftest before it was removed.
4. **Playable** — the intro advances and the Prince moves under `pop`
   shell input over CDC ACM.

**Operational note — CDC boot order.** On this board USB must be plugged
into the host *after* boot: connecting before boot makes CDC miss the host
enumeration window and the `uart:~$` prompt never appears. Boot the Pi,
then plug USB — the shell then comes up. (Same enumeration-window behavior
seen on the wipeout port.)

**Flash (user runs — Claude does not auto-flash):**
```sh
~/zephyrproject/zephyr/boards/raspberrypi/rpi_zero_2w/support/install-to-sdcard.sh \
  /Volumes/RECOVERY \
  ~/zephyrproject/build-sdlpop/zephyr/zephyr.bin && diskutil eject /Volumes/RECOVERY
```
`config.txt` needs `enable_uart=1` (mini-UART console) and HDMI hotplug;
no `arm_freq`/`force_turbo` needed for correctness (add for smoothness).
The color-bar selftest (`CONFIG_SDLPOP_VIDEO_SELFTEST`) is off now that the
present path is signed off — re-enable it for any future display bring-up.

---

## Deferred scope (§5) — designed-for, not built

- **Audio (§5a): no longer deferred.** The HDMI-audio work order
  landed `shim_audio_hdmi.c` as the real Kconfig-selected backend
  behind this exact seam (no game or shim change, as designed) — see
  `RUNTIME_REPORT_AUDIO.md`. The original "expected answer: no, use
  I2S + external DAC" was overturned: direct MAI programming works
  without VCHIQ. `shim_audio_none.c` remains the default for boards
  without the HDMI path (qemu sim).
- **Controls (§5b):** joystick/controller/haptic uniformly "none
  present" (bucket D). `shim_event.c` accepts any producer. On hardware
  **today** the input path is the `pop` shell command over the USB CDC
  ACM line (`shim_shell.c`) — you drive the game from the `uart:~$`
  prompt (`pop key enter`, `pop down right`, …). The shared HOGP
  boot-keyboard client (8BitDo Micro in K-mode) attaches at the same
  producer seam later; key-state diffing across boot reports → the same
  KEYDOWN/KEYUP submissions the scripted source and the shell command
  already use. **Sim-verified:** `pop key enter` dismisses the intro
  "Press any key to continue" screen and advances into level 1
  (before/after captures, session 2026-07-04).

## Known issues / next steps

- **[HW]** Everything in the sign-off list above — first boot on the board.
- **[audio]** RESOLVED — HDMI audio implemented via direct MAI + DMA
  (`RUNTIME_REPORT_AUDIO.md`); hardware sign-off pending there.
- **[input]** Build the HOGP keyboard module (shared with
  termdirect/tuidirect); verify 8BitDo Micro K-mode advertises as BLE HID
  before committing (fallback: Xbox Series controller).
- **[saves]** `SDLPoP.cfg`/quicksave/replay writes currently fail EROFS
  (read-only pack). Additive: back writes with Zephyr `fs_*` on the SD
  card when persistence is wanted.
- **[minor]** SDLPoP's per-boot "Failed to load sound N" spam is benign
  (audio deferred); silence by wiring the OPL/vorbis path or quieting the
  log once audio lands.
