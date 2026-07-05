#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Build the PiZZA Arduino loader + reference sketch, print the flash
# command. Run from anywhere; the script resolves all paths relative
# to itself and the user's $HOME.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ZEPHYR_WS="${HOME}/zephyrproject"
ACZ="${HOME}/github/SS/ArduinoCore-zephyr"
HAL_BROADCOM="${ZEPHYR_WS}/modules/hal/broadcom"

LOADER_BUILD="${ZEPHYR_WS}/build/pizza-arduino-loader"
SKETCH_BUILD="${ZEPHYR_WS}/build/pizza-arduino-sketch"

# Toolchain (aarch64) -- the cross-compile variant of the Zephyr SDK.
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE="${HOME}/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-"

# Zephyr venv (jsonschema, west, etc.).
# shellcheck disable=SC1091
source "${HOME}/.zephyr-venv/bin/activate"

WEST="${HOME}/.local/bin/west"

cd "${ZEPHYR_WS}"

echo "==> Building PiZZA Arduino loader (ArduinoCore-zephyr/loader)"
ZEPHYR_BASE="${ZEPHYR_WS}/zephyr" \
EXTRA_ZEPHYR_MODULES="${HAL_BROADCOM};${ACZ}" \
"${WEST}" build -p always -b rpi_zero_2w \
	-d "${LOADER_BUILD}" \
	-s "${ACZ}/loader" \
	-- -DTOOLCHAIN_HAS_GLIBCXX=ON

echo "==> Building reference sketch (apps/Arduino/sketch -> sketch.llext)"
"${WEST}" build -p always -b rpi_zero_2w \
	-d "${SKETCH_BUILD}" \
	"${HERE}/sketch"
"${WEST}" build -d "${SKETCH_BUILD}" -t sketch

LOADER_BIN="${LOADER_BUILD}/zephyr/zephyr.bin"
SKETCH_LLEXT="${SKETCH_BUILD}/sketch.llext"

echo
echo "Built:"
echo "  loader  : ${LOADER_BIN}        ($(stat -f%z "${LOADER_BIN}") bytes, md5 $(md5 -q "${LOADER_BIN}"))"
echo "  sketch  : ${SKETCH_LLEXT}      ($(stat -f%z "${SKETCH_LLEXT}") bytes, md5 $(md5 -q "${SKETCH_LLEXT}"))"
echo
echo "To flash (mounts SD as /Volumes/RECOVERY by default):"
echo
cat <<EOF
  ~/zephyrproject/zephyr/boards/raspberrypi/rpi_zero_2w/support/install-to-sdcard.sh \\
    /Volumes/RECOVERY \\
    ${LOADER_BIN} \\
    && cp ${SKETCH_LLEXT} /Volumes/RECOVERY/sketch.llext \\
    && diskutil eject /Volumes/RECOVERY
EOF
