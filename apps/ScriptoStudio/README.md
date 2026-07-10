<!-- SPDX-License-Identifier: Apache-2.0 -->
# ScriptoStudio

Legacy PINN OS descriptor for a PiZZa variant that carried a full-fat
[pyDirect](https://github.com/jetpax/pyDirect) MicroPython runtime on
Zephyr (ulab, WebREPL Binary Protocol, HTTP server). Booted into the
MicroPython REPL over USB CDC ACM; agent state persisted on a separate
SD partition (`/sd`).

PINN is retired for PiZZa; the current install path is `make-sdcard.sh`
/ `install-to-sdcard.sh` from the [repo root](../../README.md). Building
the pyDirect image today is a manual Zephyr west build against the
pyDirect app tree, not from this directory.

The two JSON files (`os.json`, `partitions.json`) are the historical
PINN inputs, retained for reference.
