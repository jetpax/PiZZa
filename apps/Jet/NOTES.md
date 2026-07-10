<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Jet on PiZZa: notes

Companion to [`README.md`](README.md).

## How it works

Links the [Jet](https://github.com/CubeCoders/Jet) software 3D rasteriser
against the Raspberry Pi Zero 2 W and pushes 912x492 RGB565 frames to
the VideoCore scanout via BCM2835 async DMA; the HVS scales to the
monitor. ARM clock is pinned to 1.0 GHz at boot via the VC mailbox.

Renders into one of two cached RGB565 backbuffers so render N+1 overlaps
the DMA blit of N; frame time is `max(render, blit) + kick`. Physics is
integrated in `src/main.cpp`: gravity bounce with energy-conserving floor
correction, X/Z wall roaming, spin coupled to horizontal velocity, axial
tilt -23 degrees.

`src/JetConfig.hpp` overrides Jet's `Config.example.hpp` for this
frontend (PHONG on, HALF_WIDTH_BUFFERS off, etc.) and is prepended on
Jet's include path so it wins.

## Layout

```
Jet/
├── README.md
├── NOTES.md
├── CMakeLists.txt         adds Jet subdir; wires zephyr_interface + JetConfig
├── Kconfig                pulls SAMPLE_USBD_* for the CDC shell
├── prj.conf               TLS on, libstdc++ subset, malloc/heap sizes
├── boards/
│   └── rpi_zero_2w.{conf,overlay}   HDMI + async DMA + CDC ACM
└── src/
    ├── JetConfig.hpp      per-frontend Config override
    └── main.cpp           scene setup, physics, per-frame render + blit
```

## Related

- Architecture explainer:
  [`notes/jet_app_architecture.html`](../../notes/jet_app_architecture.html).
- Session handover: [`HANDOVER.md`](HANDOVER.md).

## Licensing

Binary is AGPL-3.0 by linking Jet. Kept out of PizzaShell so the
Apache-2.0 image stays clean.
