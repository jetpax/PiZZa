#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Produce the PiZZA *distribution* artifacts from HEAD: builds the loader
# with the llext-edk, then runs the same post-processing as upstream
# ArduinoCore-zephyr extra/build.sh (EDK extract + comment-strip, copy
# firmwares/, gen_provides, gen_arduino_files, boards.local.txt) -- but in
# the local workspace topology (Zephyr workspace primary, ArduinoCore-zephyr
# as an EXTRA_ZEPHYR_MODULE) where extra/build.sh's bootstrap assumptions do
# not hold.
#
# Output (all consistent, from current ArduinoCore-zephyr HEAD):
#   firmwares/zephyr-rpi_zero_2w_bcm2710.{bin,elf,dts,config}
#   variants/rpi_zero_2w_bcm2710/{llext-edk,syms-*.ld,tls-syms.S,*.txt}
#   boards.local.txt
#
# Does NOT flash. The produced loader must be flashed + confirmed on hardware
# before it is packaged into a release (feedback_no_untested_release_publishes).

set -euo pipefail

ZEPHYR_WS="${HOME}/zephyrproject"
ACZ="${HOME}/github/SS/ArduinoCore-zephyr"
HAL_BROADCOM="${ZEPHYR_WS}/modules/hal/broadcom"

variant="rpi_zero_2w_bcm2710"
target="rpi_zero_2w/bcm2710"
BUILD_DIR="${ACZ}/build/${variant}"
VARIANT_DIR="${ACZ}/variants/${variant}"

export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE="${HOME}/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-"
export ZEPHYR_BASE="${ZEPHYR_WS}/zephyr"
export EXTRA_ZEPHYR_MODULES="${HAL_BROADCOM};${ACZ}"

# shellcheck disable=SC1091
source "${HOME}/.zephyr-venv/bin/activate"
WEST="${HOME}/.local/bin/west"

cd "${ACZ}"

echo "==> [1/6] Build loader + llext-edk (pristine, from HEAD)"
"${WEST}" build -p always -b "${target}" -d "${BUILD_DIR}" -s loader \
	-t llext-edk -- -DTOOLCHAIN_HAS_GLIBCXX=ON

echo "==> [2/6] Extract EDK into the variant dir"
mkdir -p "${VARIANT_DIR}" firmwares
( cd "${BUILD_DIR}" && rm -rf llext-edk && tar xf zephyr/llext-edk.tar.Z )
rsync -a --delete "${BUILD_DIR}/llext-edk" "${VARIANT_DIR}/"

echo "==> [3/6] Strip inline comments from EDK macro headers"
line_preproc_ok='^\s*#\s*(if|else|elif|endif)'
line_comment_only='^\s*\/\*'
line_continuation='\\$'
c_comment='\s*\/\*.*?\*\/'
perl -i -pe "s/${c_comment}//gs unless /${line_preproc_ok}/ || (/${line_comment_only}/ && !/${line_continuation}/)" \
	$(find "${VARIANT_DIR}/llext-edk/include/" -type f)

# The upstream llext-edk packager omits *.inc files, but arch/arm64/arch.h
# #includes <zephyr/arch/arm64/macro.inc>. Without it every Arduino sketch
# compile fails ("macro.inc: No such file or directory"). Copy it in raw
# (assembly include -- not subject to the comment-strip pass above).
edk_arm64="${VARIANT_DIR}/llext-edk/include/zephyr/include/zephyr/arch/arm64"
if [ -d "${edk_arm64}" ]; then
	cp "${ZEPHYR_BASE}/include/zephyr/arch/arm64/macro.inc" "${edk_arm64}/macro.inc"
fi

echo "==> [4/6] Copy firmwares"
for ext in elf bin hex; do
	rm -f "firmwares/zephyr-${variant}.${ext}"
	[ -f "${BUILD_DIR}/zephyr/zephyr.${ext}" ] && cp "${BUILD_DIR}/zephyr/zephyr.${ext}" "firmwares/zephyr-${variant}.${ext}"
done
cp "${BUILD_DIR}/zephyr/zephyr.dts" "firmwares/zephyr-${variant}.dts"
cp "${BUILD_DIR}/zephyr/.config" "firmwares/zephyr-${variant}.config"

echo "==> [5/6] gen_provides + gen_arduino_files"
extra/gen_provides.py "${BUILD_DIR}/zephyr/zephyr.elf" -T > "${VARIANT_DIR}/tls-syms.S"
extra/gen_provides.py "${BUILD_DIR}/zephyr/zephyr.elf" -L > "${VARIANT_DIR}/syms-dynamic.ld"
extra/gen_provides.py "${BUILD_DIR}/zephyr/zephyr.elf" -LF \
	"+kheap_llext_heap" \
	"+kheap__system_heap" \
	"*sketch_base_addr=_sketch_start" \
	"*sketch_max_size=_sketch_max_size" \
	"*loader_max_size=_loader_max_size" \
	"malloc=__wrap_malloc" \
	"free=__wrap_free" \
	"realloc=__wrap_realloc" \
	"calloc=__wrap_calloc" \
	"random=__wrap_random" > "${VARIANT_DIR}/syms-static.ld"
cmake -P extra/gen_arduino_files.cmake "${variant}"

echo "==> [6/6] Regenerate boards.local.txt"
board="pizza"
gv() { grep -E "\<${2//./\\.}\>\s*=" "$1" | tail -n 1 | cut -d '=' -f 2- | tr -d '); '; }
cat > boards.local.txt <<HDR
#########################################################################################
#
# AUTO GENERATED FILE - DO NOT EDIT
# This file is manipulated by build scripts; manual changes may be overwritten.
#
#########################################################################################

HDR
CODE_ADDR=$(gv "${VARIANT_DIR}/syms-static.ld" '_sketch_start')
CODE_SIZE=$(( $(gv "${VARIANT_DIR}/syms-static.ld" '_sketch_max_size') ))
DATA_SIZE=$(( 1024 * $(gv "firmwares/zephyr-${variant}.config" 'CONFIG_LLEXT_HEAP_SIZE') ))
MACH_CPU=$(gv "${VARIANT_DIR}/machine_flags.txt" 'mcpu'); [ -z "${MACH_CPU}" ] && MACH_CPU=$(gv "${VARIANT_DIR}/machine_flags.txt" 'march')
{
	echo "${board}.upload.address=${CODE_ADDR}"
	echo "${board}.upload.maximum_size=${CODE_SIZE}"
	echo "${board}.upload.maximum_data_size=${DATA_SIZE}"
	[ -z "${MACH_CPU}" ] || echo "${board}.build.architecture=${MACH_CPU}"
	[ -z "${MACH_CPU}" ] || echo "${board}.build.mcu=${MACH_CPU}"
} >> boards.local.txt

echo
echo "Distribution artifacts ready (from HEAD). Loader to flash + confirm:"
echo "  ${BUILD_DIR}/zephyr/zephyr.bin  ($(stat -f%z "${BUILD_DIR}/zephyr/zephyr.bin") bytes, md5 $(md5 -q "${BUILD_DIR}/zephyr/zephyr.bin"))"
echo
echo "boards.local.txt:"; cat boards.local.txt
