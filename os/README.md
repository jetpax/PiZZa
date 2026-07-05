# Legacy published-endpoint directory

The repo's application sources moved from `os/` to `apps/` (2026-07-05).

This directory exists for exactly one reason: the Arduino
Boards-Manager index URL is published in users' IDE settings and raw
GitHub URLs do not redirect, so its path is frozen forever:

    https://raw.githubusercontent.com/jetpax/PiZZa/dev/os/Arduino/package_pizza_index.json

`os/Arduino/package_pizza_index.json` is the single, canonical copy
(hand-updated on each core release — see `apps/Arduino/README.md`).

Do not add anything else here.
