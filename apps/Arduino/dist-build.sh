#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Produce the PiZZa *distribution* artifacts from HEAD for BOTH boards
# (rpi_zero_2w_bcm2710 + rpi_zero_w_bcm2835): builds each loader with
# its llext-edk, then runs the same post-processing as upstream
# ArduinoCore-zephyr extra/build.sh (EDK extract + comment-strip, copy
# firmwares/, gen_provides, gen_arduino_files, boards.local.txt) -- but
# in the local workspace topology (Zephyr workspace primary,
# ArduinoCore-zephyr as an EXTRA_ZEPHYR_MODULE) where extra/build.sh's
# bootstrap assumptions do not hold.
#
# Output (all consistent, from current ArduinoCore-zephyr HEAD):
#   firmwares/zephyr-rpi_zero_2w_bcm2710.{bin,elf,dts,config}
#   firmwares/zephyr-rpi_zero_w_bcm2835.{bin,elf,dts,config}
#   variants/rpi_zero_2w_bcm2710/{llext-edk,syms-*.ld,tls-syms.S,*.txt}
#   variants/rpi_zero_w_bcm2835/{llext-edk,syms-*.ld,tls-syms.S,*.txt}
#   boards.local.txt
#
# Does NOT flash. Both loaders must be flashed + confirmed on hardware
# before packaging into a release (feedback_no_untested_release_publishes).
#
# Usage:
#   ./dist-build.sh              # build both boards
#   ./dist-build.sh pizza        # build only the 2 W
#   ./dist-build.sh pizza_zero_w # build only the original Zero W

set -euo pipefail

ZEPHYR_WS="${HOME}/zephyrproject"
ACZ="${HOME}/github/SS/ArduinoCore-zephyr"
HAL_BROADCOM="${ZEPHYR_WS}/modules/hal/broadcom"

# Release loaders build from the `dev` worktree -- the branch that carries
# every zp staging branch plus the SMP and PWM work. Override to build
# against another worktree.
export ZEPHYR_BASE="${ZEPHYR_BASE:-${ZEPHYR_WS}/zephyr-dev}"
export EXTRA_ZEPHYR_MODULES="${HAL_BROADCOM};${ACZ}"

# shellcheck disable=SC1091
[ -f "${HOME}/.zephyr-venv/bin/activate" ] && source "${HOME}/.zephyr-venv/bin/activate"
WEST="${HOME}/.local/bin/west"
[ -x "${WEST}" ] || WEST="python3 -m west"

cd "${ACZ}"

# Per-board table: name | variant | target | cross_compile
# Extend by adding rows.
BOARDS=(
	"pizza|rpi_zero_2w_bcm2710|rpi_zero_2w/bcm2710|${HOME}/zephyr-sdk/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-"
	"pizza_zero_w|rpi_zero_w_bcm2835|rpi_zero_w/bcm2835|${HOME}/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-"
)

# Filter by CLI arg if provided. ALL_BOARDS keeps the full table:
# boards.local.txt is regenerated from it (step 6), so a filtered
# build doesn't clobber the other board's entries.
ALL_BOARDS=("${BOARDS[@]}")
if [ $# -gt 0 ]; then
	filter="$1"
	FILTERED=()
	for row in "${BOARDS[@]}"; do
		[ "${row%%|*}" = "${filter}" ] && FILTERED+=("${row}")
	done
	if [ ${#FILTERED[@]} -eq 0 ]; then
		echo "error: unknown board '${filter}'. Known: pizza pizza_zero_w"
		exit 1
	fi
	BOARDS=("${FILTERED[@]}")
fi

build_board() {
	local board="$1" variant="$2" target="$3" cross="$4"
	local BUILD_DIR="${ACZ}/build/${variant}"
	local VARIANT_DIR="${ACZ}/variants/${variant}"

	echo
	echo "###############################################################"
	echo "## Building ${board} (${target})"
	echo "###############################################################"

	export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
	export CROSS_COMPILE="${cross}"

	echo "==> [1/6] Build loader + llext-edk (pristine, from HEAD)"
	${WEST} build -p always -b "${target}" -d "${BUILD_DIR}" -s loader \
		-t llext-edk -- -DTOOLCHAIN_HAS_GLIBCXX=ON

	echo "==> [2/6] Extract EDK into the variant dir"
	mkdir -p "${VARIANT_DIR}" firmwares
	( cd "${BUILD_DIR}" && rm -rf llext-edk && tar xf zephyr/llext-edk.tar.Z )
	rsync -a --delete "${BUILD_DIR}/llext-edk" "${VARIANT_DIR}/"

	echo "==> [3/6] Strip inline comments from EDK macro headers"
	local line_preproc_ok='^\s*#\s*(if|else|elif|endif)'
	local line_comment_only='^\s*\/\*'
	local line_continuation='\\$'
	local c_comment='\s*\/\*.*?\*\/'
	perl -i -pe "s/${c_comment}//gs unless /${line_preproc_ok}/ || (/${line_comment_only}/ && !/${line_continuation}/)" \
		$(find "${VARIANT_DIR}/llext-edk/include/" -type f)

	# The upstream llext-edk packager omits *.inc files, but arch/arm64/arch.h
	# #includes <zephyr/arch/arm64/macro.inc>. Copy it in raw for the AArch64
	# variant (assembly include -- not subject to the comment-strip pass above).
	local edk_arm64="${VARIANT_DIR}/llext-edk/include/zephyr/include/zephyr/arch/arm64"
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
}

for row in "${BOARDS[@]}"; do
	IFS='|' read -r board variant target cross <<< "${row}"
	build_board "${board}" "${variant}" "${target}" "${cross}"
done

# The regenerated EDK's autoconf.h can change kernel struct layouts (e.g.
# CONFIG_POLL grows struct k_sem, which the Arduino `Serial` embeds). arduino-cli
# keys its core cache on flag *strings*, not autoconf *contents*, so it silently
# reuses a core compiled against the old config -> a runtime ABI mismatch that
# corrupts kernel objects (sys_dlist_remove -> FAR=0). Invalidate the core cache
# so the next `arduino-cli compile` rebuilds the core against the new EDKs.
rm -rf "${HOME}/Library/Caches/arduino/cores/"* \
       "${HOME}/Library/Caches/arduino/sketches/"* 2>/dev/null || true
echo
echo "Cleared arduino-cli core + per-sketch caches (EDK changed -> recompile)."

echo
echo "==> [6/6] Regenerate boards.local.txt (both boards)"
gv() { grep -E "\<${2//./\\.}\>\s*=" "$1" 2>/dev/null | tail -n 1 | cut -d '=' -f 2- | tr -d '); ' || true; }
cat > boards.local.txt <<HDR
#########################################################################################
#
# AUTO GENERATED FILE - DO NOT EDIT
# This file is manipulated by build scripts; manual changes may be overwritten.
#
#########################################################################################

HDR

for row in "${ALL_BOARDS[@]}"; do
	IFS='|' read -r board variant target cross <<< "${row}"
	VARIANT_DIR="${ACZ}/variants/${variant}"
	# A filtered run still emits every board whose artifacts exist on
	# disk from an earlier build; skip boards never built here.
	[ -f "firmwares/zephyr-${variant}.config" ] || continue
	# SD-based loaders (CONFIG_ARDUINO_SKETCH_LOADER_FS=y) don't emit
	# _sketch_start / _sketch_max_size -- those are for flash-partition
	# loaders. Only emit those upload fields if the symbols exist.
	CODE_ADDR=$(gv "${VARIANT_DIR}/syms-static.ld" '_sketch_start')
	CODE_SIZE_RAW=$(gv "${VARIANT_DIR}/syms-static.ld" '_sketch_max_size')
	DATA_SIZE_RAW=$(gv "firmwares/zephyr-${variant}.config" 'CONFIG_LLEXT_HEAP_SIZE')
	DATA_SIZE=$(( 1024 * ${DATA_SIZE_RAW:-0} ))
	MACH_CPU=$(gv "${VARIANT_DIR}/machine_flags.txt" 'mcpu')
	[ -z "${MACH_CPU}" ] && MACH_CPU=$(gv "${VARIANT_DIR}/machine_flags.txt" 'march')
	{
		[ -z "${CODE_ADDR}" ] || echo "${board}.upload.address=${CODE_ADDR}"
		[ -z "${CODE_SIZE_RAW}" ] || echo "${board}.upload.maximum_size=$(( CODE_SIZE_RAW ))"
		[ "${DATA_SIZE}" -eq 0 ] || echo "${board}.upload.maximum_data_size=${DATA_SIZE}"
		[ -z "${MACH_CPU}" ] || echo "${board}.build.architecture=${MACH_CPU}"
		[ -z "${MACH_CPU}" ] || echo "${board}.build.mcu=${MACH_CPU}"
		echo ""
	} >> boards.local.txt
done

echo
echo "Distribution artifacts ready (from HEAD). Loaders to flash + confirm:"
for row in "${BOARDS[@]}"; do
	IFS='|' read -r board variant target cross <<< "${row}"
	BUILD_DIR="${ACZ}/build/${variant}"
	echo "  ${BUILD_DIR}/zephyr/zephyr.bin  ($(stat -f%z "${BUILD_DIR}/zephyr/zephyr.bin") bytes, md5 $(md5 -q "${BUILD_DIR}/zephyr/zephyr.bin"))"
done
echo
echo "boards.local.txt:"; cat boards.local.txt
