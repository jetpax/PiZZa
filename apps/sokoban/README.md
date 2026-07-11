<!-- SPDX-License-Identifier: Apache-2.0 -->
# sokoban

[howprice/sdl2-sokoban](https://github.com/howprice/sdl2-sokoban) (MIT)
on PiZZa via the shared SDL2 shim
([`apps/lib/sdl2shim`](../lib/sdl2shim)). 640x480 ARGB8888 through the
shim's software `SDL_Renderer` engine, HVS-scaled to the panel over
the proven `bcm2835_fb` path. Text via a baked ARCADE_N glyph atlas
(no freetype), the tileset via a baked PIMG blob (no libpng), the
Tiled maps via baked S2TM records (no libxml2/zlib). No audio -- the
game makes none.

The game tree is vendored at [`game/`](game/) -- upstream is dormant;
see [`game/PROVENANCE.md`](game/PROVENANCE.md) for the pinned commit
and the three local patches.

Unlike Doom/SDLPoP/minivmac (app-side frame composition), sokoban is
written against the `SDL_Renderer` API proper, so this port drove
those pieces into the shim where the next renderer-API game (SNES9x?)
picks them up for free: `SDL2SHIM_RENDER` + `SDL2SHIM_TTF_ATLAS` +
`SDL2SHIM_IMAGE_PIMG` (+ the SDLPoP-promoted `SURFACE`/`RW` groups).
App-local here: the asset pack + fd layer, the TMX loader, the present
backends, the `sok` shell, the scripted sim timeline.

## Build

Hardware (Raspberry Pi Zero 2 W, HDMI + USB CDC ACM shell):

```sh
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=$HOME/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf- \
west build -b rpi_zero_2w -d build-sokoban-hw apps/sokoban -- \
  -DTOOLCHAIN_HAS_GLIBCXX=ON
```

Sim (qemu, scripted input, PPM frame capture via semihosting):

```sh
west build -b qemu_cortex_a53 -d build-sokoban-qemu apps/sokoban -- \
  -DTOOLCHAIN_HAS_GLIBCXX=ON
west build -d build-sokoban-qemu -t run
```

Asset baking needs Pillow (`pack_assets.py` runs under the west
python). Host test suites: `tests/pack_host` (bake the real pack, read
it back through the real device parsers) and the shim's
`tests/render_host` (renderer/TTF/PIMG pixel checks).

## Controls

Title: `O` original levels, `M` microban levels. In game: arrows move,
`R` restart, `U` undo, `P` pause, `SPACE` next level, `ESC` back to
the title (the Zephyr entry relaunches the game; an appliance has
nothing to exit to).

On `rpi_zero_2w` a Bluetooth pad or keyboard drives the game via
[`apps/lib/btinput`](../lib/btinput) (first pairing: boot inquiry
finds a discoverable pad; bonds persist on the SD card). The 8BitDo
Micro K-mode mapping (`src/sokoban_btinput.c`, legend on the title
screen): d-pad moves, `X` Original, `Y` Microban, `A` continue,
`B` undo, `L` restart, `R` pause. No pad button quits -- the pad's
power-off long-press fires Home, so Home is deliberately inert.
A real BT keyboard passes through unmapped (its O/M/R/U/arrows just
work).

Keys also arrive over the USB CDC ACM shell:

```
sok key o
sok key up [hold_ms]
sok key r
```
