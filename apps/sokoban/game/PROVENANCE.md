<!-- SPDX-License-Identifier: MIT -->
# game/ provenance

Vendored copy of howprice/sdl2-sokoban, upstream commit
`29a301f37b56ed5f8002d39eae59074a28607bef` (2019-07-30), MIT license
(see [LICENSE](LICENSE)). Upstream is dormant, so the tree is vendored
and patched in place rather than referenced externally.

Not vendored (build-system + asset sources the port doesn't use):
`3rdParty/`, `scripts/`, `tools/` (GENie executables -- binaries stay
out of this repo), `docs/`, `data/sokoban_tiles.psd`.

Local patches (all marked with a `PiZZa:` comment at the site):

- `src/App.cpp`: `<chrono>` frame timer replaced with
  `SDL_GetPerformanceCounter`/`Frequency` (the upstream `#TODO`);
  drops the only C++ stdlib dependency beyond new/delete.
- `src/Game.h`: undo `kStackSize` 1M -> 16K (`m_moveStack` was an 8 MB
  Game member).
- `src/hp_assert.h`: `HP_BREAK` gains an `__aarch64__` branch
  (`__builtin_trap()`); upstream's `__GNUC__` fallback is x86 asm.
- `src/Game.cpp`: one `printf` per state transition (title/playing/
  level-complete/game-complete) -- console tracing for headless sim
  gates and hardware bring-up.
